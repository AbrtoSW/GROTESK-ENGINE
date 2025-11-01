#include "stb/stb_image.h"
#include "vk_loader.h"

#include "vk_types.h"
#include <iostream>

#include "vk_initializers.h"
#include <glm/gtx/quaternion.hpp>
#include "vk_engine.h"
#include "vk_renderer.h"

#include "fastgltf/core.hpp"
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>


std::optional<std::vector<std::shared_ptr<MeshAsset>>> Importer::loadGltf(VulkanEngine* engine, Renderer* renderer, std::filesystem::path filePath) {

	std::cout << "Loading GLTF: " << filePath << std::endl;

	auto result = fastgltf::GltfDataBuffer::FromPath(filePath);
	if (!result) {
		fmt::print("Failed to read GLB: {}\n", fastgltf::to_underlying(result.error()));
		return {};
	}
	fastgltf::GltfDataBuffer data = std::move(result.get());

	constexpr auto gltfOptions = fastgltf::Options::LoadExternalBuffers;
	fastgltf::Asset gltf;
	fastgltf::Parser parser{};

	auto load = parser.loadGltfBinary(data, filePath.parent_path(), gltfOptions);
	if (load) gltf = std::move(load.get());
	else {
		fmt::print("Failed to load glTF: {} \n", fastgltf::to_underlying(load.error()));
		return {};
	}

	// --- Samplers ---
	std::vector<VkSampler> samplers;
	samplers.reserve(gltf.samplers.size());
	for (fastgltf::Sampler& s : gltf.samplers) {
		VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		sci.minLod = 0.f;
		sci.maxLod = VK_LOD_CLAMP_NONE;
		sci.magFilter = extract_filter(s.magFilter.value_or(fastgltf::Filter::Linear));
		sci.minFilter = extract_filter(s.minFilter.value_or(fastgltf::Filter::Linear));
		sci.mipmapMode = extract_mipmap_mode(s.minFilter.value_or(fastgltf::Filter::Linear));
		sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		VkSampler vkSampler{};
		VK_CHECK(vkCreateSampler(engine->device, &sci, nullptr, &vkSampler));
		samplers.push_back(vkSampler);
		engine->mainDeletionQueue.push_sampler(vkSampler);
	}

	// --- Images ---
	std::vector<AllocatedImage> images;
	images.reserve(gltf.images.size());
	const std::filesystem::path baseDir = filePath.parent_path();
	for (fastgltf::Image& img : gltf.images) {
		auto gpuImgOpt = vkutil::load_image(renderer, baseDir, gltf, img);
		images.push_back(gpuImgOpt ? *gpuImgOpt : renderer->errorCheckerBoardImage);
	}
	for (auto& img : images) {
		engine->mainDeletionQueue.push_allocated_image(img);
	}

	// --- Material UBO (one big buffer, one slot per glTF material) ---
	const size_t matCount = gltf.materials.size();
	AllocatedBuffer materialUBO{};
	if (matCount) {
		materialUBO = engine->create_buffer(
			sizeof(GLTFMetallic_Roughness::MaterialConstants) * matCount,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU);
		engine->mainDeletionQueue.push_allocated_buffer(materialUBO);
	}
	auto* cpu = reinterpret_cast<GLTFMetallic_Roughness::MaterialConstants*>(
		materialUBO.info.pMappedData);

	// --- Build shared materials ---
	std::vector<std::shared_ptr<GLTFMaterial>> materials;
	materials.reserve(matCount ? matCount : 1);

	for (size_t i = 0; i < matCount; ++i) {
		const auto& gm = gltf.materials[i];

		// constants
		GLTFMetallic_Roughness::MaterialConstants c{};
		c.colorFactors = glm::vec4(gm.pbrData.baseColorFactor[0],
			gm.pbrData.baseColorFactor[1],
			gm.pbrData.baseColorFactor[2],
			gm.pbrData.baseColorFactor[3]);
		c.metal_rough_factors = glm::vec4(gm.pbrData.metallicFactor,
			gm.pbrData.roughnessFactor, 0, 0);
		if (cpu) cpu[i] = c;

		// textures (fallbacks)
		AllocatedImage baseColorImg = renderer->whiteImage;
		VkSampler      baseColorSamp = renderer->defaultSamplerLinear;
		AllocatedImage metalRoughImg = renderer->whiteImage;
		VkSampler      metalRoughSamp = renderer->defaultSamplerLinear;

		if (gm.pbrData.baseColorTexture) {
			const auto& t = gltf.textures[gm.pbrData.baseColorTexture->textureIndex];
			if (t.imageIndex && *t.imageIndex < images.size())       baseColorImg = images[*t.imageIndex];
			if (t.samplerIndex && *t.samplerIndex < samplers.size()) baseColorSamp = samplers[*t.samplerIndex];
		}
		if (gm.pbrData.metallicRoughnessTexture) {
			const auto& t = gltf.textures[gm.pbrData.metallicRoughnessTexture->textureIndex];
			if (t.imageIndex && *t.imageIndex < images.size())       metalRoughImg = images[*t.imageIndex];
			if (t.samplerIndex && *t.samplerIndex < samplers.size()) metalRoughSamp = samplers[*t.samplerIndex];
		}

		// descriptor set (set = 1) for this material
		DescriptorWriter w;
		// binding 0: UBO range for this material (or zero-sized if you prefer)
		if (matCount) {
			w.write_buffer(0, materialUBO.buffer,
				sizeof(GLTFMetallic_Roughness::MaterialConstants),
				uint32_t(i * sizeof(GLTFMetallic_Roughness::MaterialConstants)),
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		}
		else {
			// no materials in file; optional: bind a dummy small UBO you keep around
		}
		// binding 1: baseColor
		w.write_image(1, baseColorImg.imageView, baseColorSamp,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		// binding 2: metallicRoughness (or reuse baseColor)
		w.write_image(2, metalRoughImg.imageView, metalRoughSamp,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

		auto mat = std::make_shared<GLTFMaterial>();
		mat->data.materialSet = DescriptorUtil::buildDescriptorSet(
			engine->device,
			renderer->metalRoughMaterial.materialLayout,
			renderer->globalDescriptorAllocator,
			w);

		mat->data.passType = (gm.alphaMode == fastgltf::AlphaMode::Blend)
			? MaterialPass::Transparent : MaterialPass::MainColor;

		materials.push_back(std::move(mat));
	}


	// --- Meshes / Surfaces ---
	std::vector<std::shared_ptr<MeshAsset>> meshes;
	std::vector<uint32_t> indices;
	std::vector<Vertex>   vertices;

	for (fastgltf::Mesh& m : gltf.meshes) {
		MeshAsset meshAsset;
		meshAsset.name = m.name;

		indices.clear();
		vertices.clear();

		for (auto&& p : m.primitives) {
			GeometrySurface srf{};
			srf.startIndex = (uint32_t)indices.size();

			size_t initial_vtx = vertices.size();

			// indices
			if (p.indicesAccessor.has_value()) {
				auto& idxAcc = gltf.accessors[*p.indicesAccessor];
				indices.reserve(indices.size() + idxAcc.count);
				fastgltf::iterateAccessor<uint32_t>(gltf, idxAcc,
					[&](uint32_t idx) { indices.push_back(idx + (uint32_t)initial_vtx); });
				srf.count = (uint32_t)idxAcc.count;
			}
			else {
				// non-indexed
				auto posAttr = p.findAttribute("POSITION");
				if (posAttr == p.attributes.end()) continue;
				auto& posAcc = gltf.accessors[posAttr->accessorIndex];
				for (uint32_t i = 0; i < posAcc.count; ++i)
					indices.push_back((uint32_t)initial_vtx + i);
				srf.count = (uint32_t)posAcc.count;
			}

			// positions
			if (auto posAttr = p.findAttribute("POSITION"); posAttr != p.attributes.end()) {
				auto& posAcc = gltf.accessors[posAttr->accessorIndex];
				vertices.resize(vertices.size() + posAcc.count);
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAcc,
					[&](glm::vec3 v, size_t i) {
						Vertex vtx{};
						vtx.position = v;
						vtx.normal = { 1,0,0 };
						vtx.color = glm::vec4(1.f);
						vtx.uv_x = 0.f; vtx.uv_y = 0.f;
						vertices[initial_vtx + i] = vtx;
					});
			}
			else {
				continue; // no POSITION -> skip primitive
			}

			// normals
			if (auto nAttr = p.findAttribute("NORMAL"); nAttr != p.attributes.end()) {
				auto& nAcc = gltf.accessors[nAttr->accessorIndex];
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, nAcc,
					[&](glm::vec3 n, size_t i) { vertices[initial_vtx + i].normal = n; });
			}

			// uvs
			if (auto uvAttr = p.findAttribute("TEXCOORD_0"); uvAttr != p.attributes.end()) {
				auto& uvAcc = gltf.accessors[uvAttr->accessorIndex];
				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, uvAcc,
					[&](glm::vec2 uv, size_t i) {
						vertices[initial_vtx + i].uv_x = uv.x;
						vertices[initial_vtx + i].uv_y = uv.y;
					});
			}

			// colors (optional)
			if (auto cAttr = p.findAttribute("COLOR_0"); cAttr != p.attributes.end()) {
				auto& cAcc = gltf.accessors[cAttr->accessorIndex];
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, cAcc,
					[&](glm::vec4 col, size_t i) { vertices[initial_vtx + i].color = col; });
			}

			// attach shared material
			if (p.materialIndex && *p.materialIndex < materials.size())
				srf.material = materials[*p.materialIndex];
			else
				srf.material = materials[0];

			meshAsset.surfaces.push_back(std::move(srf));
		}

		// optional debug: color = normal
		constexpr bool OverrideColors = true;
		if (OverrideColors) {
			for (auto& vtx : vertices) vtx.color = glm::vec4(vtx.normal, 1.f);
		}

		meshAsset.meshBuffers = engine->uploadMesh(indices, vertices);
		auto sp = std::make_shared<MeshAsset>(std::move(meshAsset));
		meshes.emplace_back(sp);
		engine->mainDeletionQueue.push_mesh_buffer_deletion(meshes.back());
	}

	return meshes;
}


VkFilter extract_filter(fastgltf::Filter filter)
{
	switch (filter) {
		// nearest samplers
	case fastgltf::Filter::Nearest:
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::NearestMipMapLinear:
		return VK_FILTER_NEAREST;

		// linear samplers
	case fastgltf::Filter::Linear:
	case fastgltf::Filter::LinearMipMapNearest:
	case fastgltf::Filter::LinearMipMapLinear:
	default:
		return VK_FILTER_LINEAR;
	}
}

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter)
{
	switch (filter) {
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::LinearMipMapNearest:
		return VK_SAMPLER_MIPMAP_MODE_NEAREST;

	case fastgltf::Filter::NearestMipMapLinear:
	case fastgltf::Filter::LinearMipMapLinear:
	default:
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}


std::optional<AllocatedImage> vkutil::load_image(Renderer* renderer, const std::filesystem::path& filePath)
{
	int width = 0, height = 0, channels = 0;
	bool isHeightmap = filePath.filename().string().find("Height") != std::string::npos;

	if (isHeightmap) {
		// try 16-bit first
		stbi_us* data16 = stbi_load_16(filePath.string().c_str(), &width, &height, &channels, 1);
		if (data16) {
			VkExtent3D extent{ (uint32_t)width, (uint32_t)height, 1 };
			AllocatedImage img = renderer->create_image(
				data16, extent, VK_FORMAT_R16_UNORM,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
				/*generateMips=*/true);
			stbi_image_free(data16);
			return img;
		}

		// fallback to 8-bit
		stbi_uc* data8 = stbi_load(filePath.string().c_str(), &width, &height, &channels, 1);
		if (!data8) return {};
		{
			VkExtent3D extent{ (uint32_t)width, (uint32_t)height, 1 };
			AllocatedImage img = renderer->create_image(
				data8, extent, VK_FORMAT_R8_UNORM,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
				/*generateMips=*/true);
			stbi_image_free(data8);
			return img;
		}
	}
	else {
		// diffuse / albedo as sRGB
		stbi_uc* data = stbi_load(filePath.string().c_str(), &width, &height, &channels, 4);
		if (!data) return {};

		VkExtent3D extent{ (uint32_t)width, (uint32_t)height, 1 };
		AllocatedImage img = renderer->create_image(
			data, extent, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			/*generateMips=*/true);

		stbi_image_free(data);
		return img;
	}
}

std::optional<AllocatedImage> vkutil::load_image(Renderer* renderer, const std::filesystem::path& baseDir, fastgltf::Asset& asset, fastgltf::Image& image)
{
	AllocatedImage newImage{};
	int width = 0, height = 0, nrChannels = 0;

	std::visit(
		fastgltf::visitor{
			[](auto&) {},

			// --- URI (external file) ---
			[&](fastgltf::sources::URI& filePath) {
				if (!filePath.uri.isLocalPath()) return;
				if (filePath.fileByteOffset != 0) return;

				std::filesystem::path rel(filePath.uri.path());           // path() → string_view
				std::filesystem::path resolved = (baseDir / rel).lexically_normal();

				// Fallback for cases where the source was TGA/PSD but you only have PNG
				if (!std::filesystem::exists(resolved)) {
					auto pngTry = resolved; pngTry += ".png";
					if (std::filesystem::exists(pngTry)) resolved = pngTry;
				}

				stbi_uc* data = stbi_load(resolved.string().c_str(), &width, &height, &nrChannels, 4);
				if (data) {
					VkExtent3D imageSize{ (uint32_t)width, (uint32_t)height, 1u };
					newImage = renderer->create_image(
						data, imageSize, VK_FORMAT_R8G8B8A8_UNORM,
						VK_IMAGE_USAGE_SAMPLED_BIT, false);
					stbi_image_free(data);
				}
			},

		// --- Embedded bytes (Vector) ---
		[&](fastgltf::sources::Vector& vector) {
			const stbi_uc* bytes = reinterpret_cast<const stbi_uc*>(vector.bytes.data());
			stbi_uc* data = stbi_load_from_memory(
				bytes, static_cast<int>(vector.bytes.size()),
				&width, &height, &nrChannels, 4);
			if (data) {
				VkExtent3D imageSize{ (uint32_t)width, (uint32_t)height, 1u };
				newImage = renderer->create_image(
					data, imageSize, VK_FORMAT_R8G8B8A8_UNORM,
					VK_IMAGE_USAGE_SAMPLED_BIT, false);
				stbi_image_free(data);
			}
		},

		// --- Embedded via bufferView in a .glb ---
		[&](fastgltf::sources::BufferView& view) {
			auto& bufferView = asset.bufferViews[view.bufferViewIndex];
			auto& buffer = asset.buffers[bufferView.bufferIndex];

			std::visit(fastgltf::visitor{
				[](auto&) {},

				// GLB binary already in memory
				[&](fastgltf::sources::Vector& vector) {
					const stbi_uc* start = reinterpret_cast<const stbi_uc*>(
						vector.bytes.data() + bufferView.byteOffset);
					stbi_uc* data = stbi_load_from_memory(
						start, static_cast<int>(bufferView.byteLength),
						&width, &height, &nrChannels, 4);
					if (data) {
						VkExtent3D imageSize{ (uint32_t)width, (uint32_t)height, 1u };
						newImage = renderer->create_image(
							data, imageSize, VK_FORMAT_R8G8B8A8_UNORM,
							VK_IMAGE_USAGE_SAMPLED_BIT, false);
						stbi_image_free(data);
					}
				},

				// Some fastgltf builds store GLB buffer as Array
				[&](fastgltf::sources::Array& array) {
					const stbi_uc* start = reinterpret_cast<const stbi_uc*>(
						array.bytes.data() + bufferView.byteOffset);
					stbi_uc* data = stbi_load_from_memory(
						start, static_cast<int>(bufferView.byteLength),
						&width, &height, &nrChannels, 4);
					if (data) {
						VkExtent3D imageSize{ (uint32_t)width, (uint32_t)height, 1u };
						newImage = renderer->create_image(
							data, imageSize, VK_FORMAT_R8G8B8A8_UNORM,
							VK_IMAGE_USAGE_SAMPLED_BIT, false);
						stbi_image_free(data);
					}
				},

				// External .bin buffer on disk
				[&](fastgltf::sources::URI& uri) {
					if (!uri.uri.isLocalPath()) return;
					std::filesystem::path binPath = (baseDir / std::filesystem::path(uri.uri.path())).lexically_normal();

					std::ifstream f(binPath, std::ios::binary);
					if (!f) return;
					f.seekg(0, std::ios::end);
					const size_t fileSize = static_cast<size_t>(f.tellg());
					if (bufferView.byteOffset + bufferView.byteLength > fileSize) return;

					std::vector<uint8_t> tmp(fileSize);
					f.seekg(0, std::ios::beg);
					f.read(reinterpret_cast<char*>(tmp.data()), fileSize);

					const stbi_uc* start = reinterpret_cast<const stbi_uc*>(
						tmp.data() + bufferView.byteOffset);
					stbi_uc* data = stbi_load_from_memory(
						start, static_cast<int>(bufferView.byteLength),
						&width, &height, &nrChannels, 4);
					if (data) {
						VkExtent3D imageSize{ (uint32_t)width, (uint32_t)height, 1u };
						newImage = renderer->create_image(
							data, imageSize, VK_FORMAT_R8G8B8A8_UNORM,
							VK_IMAGE_USAGE_SAMPLED_BIT, false);
						stbi_image_free(data);
					}
				}
			}, buffer.data);
		},
		},
		image.data);

	if (newImage.image == VK_NULL_HANDLE) return {};
	
	return newImage;
}


