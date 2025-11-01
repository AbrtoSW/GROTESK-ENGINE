#pragma once

#include "vk_types.h"
#include <filesystem>
#include <fastgltf/types.hpp>

class VulkanEngine;
class Renderer;

namespace fastgltf {
	class Asset;
	class Image;
}

struct GLTFMaterial {
	MaterialInstance data;
};

struct GeometrySurface {
	uint32_t startIndex;
	uint32_t count;

	std::shared_ptr<GLTFMaterial> material;
};

struct MeshAsset {
	std::string name;
	std::vector<GeometrySurface> surfaces;
	GPUMeshBuffers meshBuffers;
	uint32_t instanceCount = 1; 
};
struct GltfAssets {
	std::vector<std::shared_ptr<MeshAsset>> meshes;
	std::vector<AllocatedImage> images;
	std::vector<VkSampler> samplers;
	std::vector<std::shared_ptr<GLTFMaterial>> materials;
	AllocatedBuffer materialDataBuffer{};
};

namespace vkutil {
	std::optional<AllocatedImage> load_image(Renderer*, const std::filesystem::path& filePath);
	std::optional<AllocatedImage> load_image(Renderer* renderer, const std::filesystem::path& baseDir, fastgltf::Asset& asset, fastgltf::Image& image);

}


VkFilter	extract_filter(fastgltf::Filter filter);
VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter);


namespace Importer {
	std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltf(VulkanEngine* engine, Renderer* renderer, std::filesystem::path filePath);
}

