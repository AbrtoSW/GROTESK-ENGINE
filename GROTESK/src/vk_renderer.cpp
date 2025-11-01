#include "vk_renderer.h"
#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_images.h"
#include "vk_pipelines.h"
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_sdl3.h"
#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"
#include "glm/glm.hpp"
#include "glm/gtx/transform.hpp"
#include <algorithm>


Renderer::Renderer(VulkanEngine& engine) : engine(engine){}


Renderer::~Renderer() {
	init_renderer_cleanup();
}

void Renderer::init_renderer() {
	glslang::InitializeProcess();
	create_draw_image_renderpass();
	create_swapchain_renderpass();
	init_framebuffers();
	init_descriptors();
	init_pipelines();
	init_imgui();
	init_default_data();
}

void Renderer::init_pipelines() {
	init_terrain_compute_pipeline();
	init_terrain_mesh_pipeline();
	init_terrain_brush_pipeline();
	metalRoughMaterial.build_pipelines(&engine, this);
}



void Renderer::render_frame(const std::vector<RenderItem>& item, const RenderContext& context) {


	FrameData& frame = engine.get_current_frame();
	

	VK_CHECK(vkWaitForFences(engine.device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));

	uint32_t swapchainImageIndex = 0;

	VkResult result = vkAcquireNextImageKHR(engine.device, engine.swapchain, 1000000000, frame.swapchainSemaphore, nullptr, &swapchainImageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		engine.resize_requested = true;
		return;
	}

	VkSemaphore currentRenderSemaphore = engine.swapchainImageRenderSemaphores[swapchainImageIndex];

	drawExtent.width = std::min(engine.swapchainExtent.width, engine.drawImage.imageExtent.width);
	drawExtent.height = std::min(engine.swapchainExtent.height, engine.drawImage.imageExtent.height);

	VK_CHECK(vkResetFences(engine.device, 1, &frame.renderFence));


	VkCommandBuffer cmd = frame.mainCommandBuffer;

	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	static constexpr float GPU_SWITCH_TEXELS = 10000.0f;

	for (const BrushStroke& stroke : editorBrushQueue) {
		const float rTex = stroke.radiusWorld / stroke.worldScale;              // radius in texels
		const float texelArea = 3.14159265f * rTex * rTex;                      // area in texels^2

		if (texelArea >= GPU_SWITCH_TEXELS) {
			// large brush -> GPU
			fmt::print("GPU brush used (radiusWorld = {:.2f}, area = {:.1f})\n", stroke.radiusWorld, texelArea);
			gpu_brush_apply(cmd, stroke);
		}
		else {

			fmt::print("CPU brush used (radiusWorld = {:.2f}, area = {:.1f})\n", stroke.radiusWorld, texelArea);
			// small brush -> CPU (your existing 3-stage flow)
			BrushStageCtx ctx{};
			cpu_brush_stage_read(cmd, stroke, ctx);

			// flush read pass
			VK_CHECK(vkEndCommandBuffer(cmd));
			auto ci = vkinit::command_buffer_submit_info(cmd);
			auto si = vkinit::submit_info(&ci, nullptr, nullptr);
			VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			VkFence fenceA = VK_NULL_HANDLE;
			VK_CHECK(vkCreateFence(engine.device, &fci, nullptr, &fenceA));
			VK_CHECK(vkQueueSubmit2(engine.graphicsQueue, 1, &si, fenceA));
			VK_CHECK(vkWaitForFences(engine.device, 1, &fenceA, VK_TRUE, UINT64_MAX));
			vkDestroyFence(engine.device, fenceA, nullptr);

			// CPU modify
			cpu_brush_stage_cpu_modify(stroke, ctx);

			// resume cmd for write pass
			VK_CHECK(vkResetCommandBuffer(cmd, 0));
			VkCommandBufferBeginInfo cbi2 = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			VK_CHECK(vkBeginCommandBuffer(cmd, &cbi2));

			cpu_brush_stage_write(cmd, ctx);
		}
	}
	editorBrushQueue.clear();

	terrain_build_indirect(cmd);

	init_draw_image_renderpass(cmd, item, context);

	vkutil::transition_image(cmd, engine.swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vkutil::copy_image_to_image(cmd, engine.drawImage.image, engine.swapchainImages[swapchainImageIndex], drawExtent, engine.swapchainExtent);
	vkutil::transition_image(cmd, engine.swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);


	init_swapchain_renderpass(cmd, swapchainImageIndex);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);

	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, frame.swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, currentRenderSemaphore);

	VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(engine.graphicsQueue, 1, &submit, frame.renderFence));

	// as its necessary that drawing commands have finished before the image is displayed to the user
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &engine.swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &currentRenderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VkResult presentResult = vkQueuePresentKHR(engine.graphicsQueue, &presentInfo);
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
		engine.resize_requested = true;
		return;
	}

	//increase the number of frames drawn
	engine.frameNumber++;
}


void Renderer::init_renderer_cleanup() {

	// this could be a bad hack because i want it to be in the main deletion queue but im destroying the pipelinecache here, 
	// i should rethink how i go about creating the pipelinecache but remember
	// that static allows for the struct to own the cache so it might be better to just keep it static and fix the vk_types.h
	// i need to rethink the vk_types.h file possibly add cpp file because
	// soon i might run into circular linkage errors 

	ImGui_ImplSDL3_Shutdown();
	glslang::FinalizeProcess();
}

void Renderer::init_swapchain_renderpass(VkCommandBuffer cmd, uint32_t imageIndex) {
	if (engine.device == VK_NULL_HANDLE) {
		throw std::runtime_error("Cannot initialize render pass: invalid device");
	}
	if (engine.swapchainImageFormat == VK_FORMAT_UNDEFINED) {
		throw std::runtime_error("Cannot initialize render pass: invalid swapchain image format");
	}

	VkClearValue clearValue{};
	clearValue = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderPassBeginInfo renderPassBeginInfo{};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = swapchainRenderPass;
	renderPassBeginInfo.framebuffer = swapchainFrameBuffers[imageIndex];
	renderPassBeginInfo.renderArea.extent = { engine.swapchainExtent.width, engine.swapchainExtent.height };
	renderPassBeginInfo.renderArea.offset = { 0,0 };

	renderPassBeginInfo.clearValueCount = 1;
	renderPassBeginInfo.pClearValues = &clearValue;


	//start rendering 
	vkCmdBeginRenderPass(cmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	render_imgui(cmd);

	vkCmdEndRenderPass(cmd);
	//end rendering 

}

void Renderer::init_draw_image_renderpass(VkCommandBuffer cmd, const std::vector<RenderItem>& item, const RenderContext& context) {
	if (engine.device == VK_NULL_HANDLE) {
		throw std::runtime_error("Cannot initialize render pass: invalid device");
	}
	if (engine.drawImage.imageFormat == VK_FORMAT_UNDEFINED) {
		throw std::runtime_error("Cannot initialize render pass: invalid draw image format");
	}
	if (engine.depthImage.imageFormat == VK_FORMAT_UNDEFINED) {
		throw std::runtime_error("Cannot initialize render pass: invalid depth image format");
	}


	std::array<VkClearValue, 2> clearValues{};
	clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f }; // color attachment
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassBeginInfo{};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = drawImageRenderPass;
	renderPassBeginInfo.framebuffer = drawImageFrameBuffer;
	renderPassBeginInfo.renderArea.extent = { drawExtent.width, drawExtent.height };
	renderPassBeginInfo.renderArea.offset = { 0,0 };

	renderPassBeginInfo.clearValueCount = (uint32_t)(clearValues.size());
	renderPassBeginInfo.pClearValues = clearValues.data();


	//start rendering 
	vkCmdBeginRenderPass(cmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	render_pass_geometry(cmd, item, context);

	vkCmdEndRenderPass(cmd);
}

void Renderer::init_framebuffers() {
	create_draw_image_framebuffer();
	create_swapchain_framebuffer();
}

void Renderer::init_descriptors() {


	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes =
	{
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }
	};

	globalDescriptorAllocator.init(engine.device, 10, sizes);
	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		drawImageDescriptorLayout = builder.build(engine.device, VK_SHADER_STAGE_COMPUTE_BIT);
	}
	drawImageDescriptors = globalDescriptorAllocator.allocate(engine.device, drawImageDescriptorLayout);

	{
		DescriptorWriter writer;
		writer.write_image(0, engine.drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.update_set(engine.device, drawImageDescriptors);
	}

	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		gpuSceneDataDescriptorLayout = builder.build(engine.device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		singleImageDescriptorLayout = builder.build(engine.device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	heightmapTexSet = globalDescriptorAllocator.allocate(engine.device, singleImageDescriptorLayout);
	heightmapDiffuseSet = globalDescriptorAllocator.allocate(engine.device, singleImageDescriptorLayout);


	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		builder.add_binding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

		terrainInstanceDescriptorLayout = builder.build(engine.device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	terrainSet = globalDescriptorAllocator.allocate(engine.device, terrainInstanceDescriptorLayout);

	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); 
		builder.add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); 
		builder.add_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); 
		terrainBuildDescriptorLayout = builder.build(engine.device, VK_SHADER_STAGE_COMPUTE_BIT);
	}
	terrainBuildSet = globalDescriptorAllocator.allocate(engine.device, terrainBuildDescriptorLayout);



	// ---- GPU brush: storage image set (set = 0 for compute) ----
	{
		VkDescriptorSetLayoutBinding b0{};
		b0.binding = 0;
		b0.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		b0.descriptorCount = 1;
		b0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		lci.bindingCount = 1; lci.pBindings = &b0;
		VK_CHECK(vkCreateDescriptorSetLayout(engine.device, &lci, nullptr, &terrainBrushDescriptorLayout));

	}

	globalDescriptorAllocator.defer_pool_main_deletion(engine.mainDeletionQueue);
	engine.mainDeletionQueue.push_descriptor_set_layout(drawImageDescriptorLayout);
	engine.mainDeletionQueue.push_descriptor_set_layout(gpuSceneDataDescriptorLayout);
	engine.mainDeletionQueue.push_descriptor_set_layout(singleImageDescriptorLayout);
	engine.mainDeletionQueue.push_descriptor_set_layout(terrainInstanceDescriptorLayout);
	engine.mainDeletionQueue.push_descriptor_set_layout(terrainBuildDescriptorLayout);
	engine.mainDeletionQueue.push_descriptor_set_layout(terrainBrushDescriptorLayout);

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
		};

		engine.frames[i].frameDescriptors = std::make_unique<DescriptorAllocatorGrowable>();
		engine.frames[i].frameDescriptors->init(engine.device, 1000, frame_sizes);

		engine.frames[i].cameraUBO = engine.create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		engine.frames[i].cameraSet = engine.frames[i].frameDescriptors->allocate(engine.device, gpuSceneDataDescriptorLayout);


		VmaAllocationInfo allocInfo;
		vmaGetAllocationInfo(engine.vmaAllocator, engine.frames[i].cameraUBO.allocation, &allocInfo);
		engine.frames[i].cameraUBOMapped = allocInfo.pMappedData;

		engine.mainDeletionQueue.push_allocated_buffer(engine.frames[i].cameraUBO);
	

		DescriptorWriter writer;
		writer.write_buffer(0, engine.frames[i].cameraUBO.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER); writer.update_set(engine.device, engine.frames[i].cameraSet);

		engine.frames[i].frameDescriptors->defer_pool_main_deletion(engine.mainDeletionQueue);
	}

}


void Renderer::init_terrain_mesh_pipeline() {
	heightMapPipeline.type = PipelineType::Graphics;

	heightMapPipeline.shaderType.vertexShader.file = { "heightmap.vert" };
	heightMapPipeline.shaderType.fragmentShader.file = { "heightmap.frag" };
	heightMapPipeline.shaderType.vertexShader.lastModified = ShaderUtil::getFileTimeStamp(heightMapPipeline.shaderType.vertexShader.file.path);
	heightMapPipeline.shaderType.fragmentShader.lastModified = ShaderUtil::getFileTimeStamp(heightMapPipeline.shaderType.fragmentShader.file.path);
	heightMapPipeline.shaderType.vertexShader.stage = VK_SHADER_STAGE_VERTEX_BIT;
	heightMapPipeline.shaderType.fragmentShader.stage = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkShaderModule vertexShader = VK_NULL_HANDLE;
	VkShaderModule fragmentShader = VK_NULL_HANDLE;

	try {
		vertexShader = ShaderUtil::compileToSPV(engine.device, heightMapPipeline.shaderType.vertexShader.file.path, EShLangVertex);
		fragmentShader = ShaderUtil::compileToSPV(engine.device, heightMapPipeline.shaderType.fragmentShader.file.path, EShLangFragment);
	}
	catch (const std::exception& e) {
		fmt::print("Shader compile error:\n{}\n", e.what());
		if (vertexShader)  vkDestroyShaderModule(engine.device, vertexShader, nullptr);
		if (fragmentShader) vkDestroyShaderModule(engine.device, fragmentShader, nullptr);
		return;
	}
	if (!vertexShader || !fragmentShader) {
		if (vertexShader)  vkDestroyShaderModule(engine.device, vertexShader, nullptr);
		if (fragmentShader) vkDestroyShaderModule(engine.device, fragmentShader, nullptr);
		return;
	}

	auto* hmConfig = heightMapPipeline.getGraphicsConfig();

	hmConfig->pushConstantRange.offset = 0;
	hmConfig->pushConstantRange.size = sizeof(GPUDrawPushConstants);
	hmConfig->pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	hmConfig->layoutInfo = vkinit::pipeline_layout_create_info();
	hmConfig->layoutInfo.pPushConstantRanges = &hmConfig->pushConstantRange;
	hmConfig->layoutInfo.pushConstantRangeCount = 1;

	hmConfig->setLayouts = { gpuSceneDataDescriptorLayout, terrainInstanceDescriptorLayout, singleImageDescriptorLayout, singleImageDescriptorLayout};
	hmConfig->layoutInfo.pSetLayouts = hmConfig->setLayouts.data();
	hmConfig->layoutInfo.setLayoutCount = (uint32_t)hmConfig->setLayouts.size();

	if (vkCreatePipelineLayout(engine.device, &hmConfig->layoutInfo, nullptr, &heightMapPipeline.pLayout) != VK_SUCCESS) {
		fmt::print("vkCreatePipelineLayout failed\n");
		vkDestroyShaderModule(engine.device, vertexShader, nullptr);
		vkDestroyShaderModule(engine.device, fragmentShader, nullptr);
		return;
	}


	PipelineBuilder pipelineBuilder;
	pipelineBuilder.clear();
	pipelineBuilder.res->pLayout = heightMapPipeline.pLayout;
	pipelineBuilder.set_shaders(vertexShader, fragmentShader);
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL); 
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	pipelineBuilder.set_multisampling_none();
	pipelineBuilder.disable_blending();
	pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_LESS);
	pipelineBuilder.set_renderpass(drawImageRenderPass);

	heightMapPipeline.pipeline = pipelineBuilder.build_pipeline(engine.device, RenderMode::Classic, &heightMapPipeline);

	newManager.registerPipeline(heightMapPipeline, Hotloadable::True, "heightMapP");

	vkDestroyShaderModule(engine.device, vertexShader, nullptr);
	vkDestroyShaderModule(engine.device, fragmentShader, nullptr);
}


void Renderer::init_terrain_compute_pipeline() {
	terrainCompute.type = PipelineType::Compute;

	terrainCompute.shaderType.computeShader.file = { "terrain_build_draws.comp" };

	VkShaderModule cs = ShaderUtil::compileToSPV(
		engine.device,
		terrainCompute.shaderType.computeShader.file.path,
		EShLangCompute
	);

	// push constants: PC_TerrainBuild { uint firstIndex, indexCount; int vertexOffset; uint tileCount; }
	VkPushConstantRange pc{};
	pc.offset = 0;
	pc.size = sizeof(PC_TerrainBuild);
	pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	// pipeline layout: set=1 → terrainBuildDescriptorLayout
	VkPipelineLayoutCreateInfo plci = vkinit::pipeline_layout_create_info();
	VkDescriptorSetLayout setLayouts[] = { terrainBuildDescriptorLayout };
	plci.setLayoutCount = 1;
	plci.pSetLayouts = setLayouts;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pc;

	VK_CHECK(vkCreatePipelineLayout(engine.device, &plci, nullptr, &terrainCompute.pLayout));

	VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = cs;
	stage.pName = "main";

	VkComputePipelineCreateInfo ci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	ci.stage = stage;
	ci.layout = terrainCompute.pLayout;

	VK_CHECK(vkCreateComputePipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &terrainCompute.pipeline));

	vkDestroyShaderModule(engine.device, cs, nullptr);


	engine.mainDeletionQueue.push_pipeline(terrainCompute.pipeline);
	engine.mainDeletionQueue.push_pipeline_layout(terrainCompute.pLayout);

}

void Renderer::init_terrain_gpu_resources(uint32_t maxTiles, bool useExternalHM, bool useTexture) {
	std::optional<AllocatedImage> hmOpt;
	if (useExternalHM) {
		const auto hmPath = Asset("textures/Rugged Terrain Height Map PNG.png");
		fmt::print("[assets] heightmap resolved to: {}\n", hmPath.string());

		if (std::filesystem::exists(hmPath)) {
			hmOpt = vkutil::load_image(this, hmPath.string().c_str());
		}
		else {
			fmt::print("[assets] missing heightmap {}\n", hmPath.string());
		}
	}

	// --- caps / formats / sizes ---
	const uint32_t slices = std::min(maxTiles, 256u);
	const uint32_t desiredH = 1024; // pick 1024/2048 according to VRAM
	const VkFormat heightFmt = hmOpt ? hmOpt->imageFormat : VK_FORMAT_R16_SFLOAT;
	const uint32_t H = desiredH;

	// --- heightmap array (≤256 layers) ---
	heightmapImage = create_image_array({ H, H, 1 }, slices, heightFmt,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		/*mipmapped*/ true);
	engine.mainDeletionQueue.push_allocated_image(heightmapImage);

	// --- init array content ---
	if (hmOpt) {
		engine.immediateCommandSubmit([&](VkCommandBuffer cmd) {
			// src -> TRANSFER_SRC
			vkutil::transition_image(cmd, hmOpt->image,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);

			// dst slice 0 -> TRANSFER_DST
			vkutil::transition_image(cmd, heightmapImage.image,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);

			// copy or blit into slice 0
			if (hmOpt->imageExtent.width == H && hmOpt->imageExtent.height == H) {
				VkImageCopy copy{};
				copy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
				copy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
				copy.extent = { H, H, 1 };
				vkCmdCopyImage(cmd, hmOpt->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					heightmapImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
			}
			else {
				VkImageBlit2 blit{ VK_STRUCTURE_TYPE_IMAGE_BLIT_2 };
				blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
				blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
				blit.srcOffsets[0] = { 0, 0, 0 };
				blit.srcOffsets[1] = { int(hmOpt->imageExtent.width), int(hmOpt->imageExtent.height), 1 };
				blit.dstOffsets[0] = { 0, 0, 0 };
				blit.dstOffsets[1] = { int(H), int(H), 1 };
				VkBlitImageInfo2 bi{ VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2 };
				bi.srcImage = hmOpt->image;                 bi.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				bi.dstImage = heightmapImage.image;         bi.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				bi.filter = VK_FILTER_LINEAR;               bi.regionCount = 1; bi.pRegions = &blit;
				vkCmdBlitImage2(cmd, &bi);
			}

			// mips for slice 0
			vkutil::generate_mipmaps(cmd, heightmapImage.image, { H, H }, 0, 1);

			// --- NEW: clone slice 0 into all layers, then gen mips per layer ---
			vkutil::transition_image(cmd, heightmapImage.image,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, /*baseLayer*/0, /*layers*/1);

			for (uint32_t layer = 1; layer < slices; ++layer) {
				// dst layer -> TRANSFER_DST
				vkutil::transition_image(cmd, heightmapImage.image,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, /*baseLayer*/layer, /*layers*/1);

				VkImageCopy c{};
				c.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, /*baseArrayLayer*/0, 1 };
				c.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, /*baseArrayLayer*/layer, 1 };
				c.extent = { H, H, 1 };

				vkCmdCopyImage(cmd,
					heightmapImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					heightmapImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &c);

				vkutil::generate_mipmaps(cmd, heightmapImage.image, { H, H }, /*baseArrayLayer*/layer, /*layerCount*/1);
			}

			// restore slice 0 to sampled
			vkutil::transition_image(cmd, heightmapImage.image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, /*baseLayer*/0, /*layers*/1);
			});
		engine.mainDeletionQueue.push_allocated_image(*hmOpt);
	}
	else {
		// no PNG: clear all slices to 0.5 and gen mips
		engine.immediateCommandSubmit([&](VkCommandBuffer cmd) {
			for (uint32_t layer = 0; layer < slices; ++layer) {
				vkutil::transition_image(cmd, heightmapImage.image,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1);

				VkImageSubresourceRange range{};
				range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				range.baseMipLevel = 0; range.levelCount = 1;
				range.baseArrayLayer = layer; range.layerCount = 1;

				VkClearColorValue clear{}; clear.float32[0] = 0.5f;
				vkCmdClearColorImage(cmd, heightmapImage.image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

				vkutil::generate_mipmaps(cmd, heightmapImage.image, { H, H }, layer, 1);
			}
			});
	}

	// --- GPU BRUSH: storage image descriptor (set=0) ---
	if (terrainBrushSet == VK_NULL_HANDLE) {
		terrainBrushSet = globalDescriptorAllocator.allocate(engine.device, terrainBrushDescriptorLayout);
	}
	{
		DescriptorWriter w;
		// storage image => no sampler, layout GENERAL (compute path will SRV<->GENERAL via barriers)
		w.write_image(
			/*binding*/ 0,
			/*imageView*/ heightmapImage.imageView,
			/*sampler*/ VK_NULL_HANDLE,
			/*imageLayout*/ VK_IMAGE_LAYOUT_GENERAL,
			/*type*/ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
		);
		w.update_set(engine.device, terrainBrushSet);
	}

	// --- diffuse (as-is) ---
	{
		bool haveDiffuse = false;

		const std::filesystem::path diffPath = Asset("textures/Rugged Terrain Diffuse PNG.png");
		fmt::print("diffuse path resolved to: {}\n", diffPath.string());

		if (useTexture && std::filesystem::exists(diffPath)) {
			if (auto diffOpt = vkutil::load_image(this, diffPath.string().c_str())) {
				heightmapDiffuseImage = *diffOpt;
				engine.mainDeletionQueue.push_allocated_image(heightmapDiffuseImage);
				haveDiffuse = (heightmapDiffuseImage.imageView != VK_NULL_HANDLE);
			}
			else {
				fmt::print("diffuse load failed, will use white fallback\n");
			}
		}
		else {
			fmt::print("diffuse not found, will use white fallback\n");
		}

		if (!haveDiffuse) {
			uint32_t white = 0xFFFFFFFF;
			VkExtent3D onePixel = { 1, 1, 1 };
			heightmapDiffuseImage = create_image(&white, onePixel,
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				/*mipmapped*/ false);
			engine.mainDeletionQueue.push_allocated_image(heightmapDiffuseImage);
		}
	}
	// --- samplers ---
	VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	sci.magFilter = VK_FILTER_LINEAR;
	sci.minFilter = VK_FILTER_LINEAR;
	sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sci.minLod = 0.0f;
	sci.maxLod = VK_LOD_CLAMP_NONE;
	VK_CHECK(vkCreateSampler(engine.device, &sci, nullptr, &heightmapSampler));
	engine.mainDeletionQueue.push_sampler(heightmapSampler);
	VkSamplerCreateInfo dsi = sci;
	dsi.addressModeU = dsi.addressModeV = dsi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	VK_CHECK(vkCreateSampler(engine.device, &dsi, nullptr, &diffuseSampler));
	engine.mainDeletionQueue.push_sampler(diffuseSampler);

	// --- buffers ---
	terrainInstanceBuffer = engine.create_buffer(
		sizeof(TerrainInstance) * maxTiles,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU);
	engine.mainDeletionQueue.push_allocated_buffer(terrainInstanceBuffer); // <<< add

	terrainIndirectDraws = engine.create_buffer(
		sizeof(VkDrawIndexedIndirectCommand) * maxTiles,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);
	engine.mainDeletionQueue.push_allocated_buffer(terrainIndirectDraws);  // <<< add

	terrainDrawCount = engine.create_buffer(
		sizeof(uint32_t),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);
	engine.mainDeletionQueue.push_allocated_buffer(terrainDrawCount);      // <<< add

	brushUBO = engine.create_buffer(
		sizeof(BrushUBO),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU);
	engine.mainDeletionQueue.push_allocated_buffer(brushUBO);              // <<< add

	// --- descriptor writes ---
	{
		DescriptorWriter w;
		w.write_buffer(0, terrainInstanceBuffer.buffer, sizeof(TerrainInstance) * maxTiles, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		w.write_buffer(1, terrainIndirectDraws.buffer, sizeof(VkDrawIndexedIndirectCommand) * maxTiles, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		w.write_buffer(2, terrainDrawCount.buffer, sizeof(uint32_t), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		w.update_set(engine.device, terrainBuildSet);
	}
	{
		DescriptorWriter w;
		w.write_image(0, heightmapImage.imageView, heightmapSampler,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		w.update_set(engine.device, heightmapTexSet);
	}
	{
		if (heightmapDiffuseImage.imageView != VK_NULL_HANDLE) {
			DescriptorWriter w;
			w.write_image(0, heightmapDiffuseImage.imageView, diffuseSampler,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
			w.update_set(engine.device, heightmapDiffuseSet);
		}
	}
	{
		DescriptorWriter w;
		w.write_buffer(0, terrainInstanceBuffer.buffer, sizeof(TerrainInstance) * maxTiles, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		w.write_buffer(3, brushUBO.buffer, sizeof(BrushUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER); // keep this
		w.update_set(engine.device, terrainSet);
	}
}



void Renderer::init_terrain_brush_pipeline() {
	terrainBrushCompute.type = PipelineType::Compute;
	terrainBrushCompute.shaderType.computeShader.file = { "terrain_brush.comp" };

	// compile shader
	VkShaderModule cs = ShaderUtil::compileToSPV(
		engine.device,
		terrainBrushCompute.shaderType.computeShader.file.path,
		EShLangCompute
	);

	// push constants = PC_Brush (32 bytes: layer,x0,y0,cx,cy,rTex,strength,mode)
	VkPushConstantRange pc{};
	pc.offset = 0;
	pc.size = 32;
	pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	// layout: set=0 -> terrainBrushDescriptorLayout
	VkPipelineLayoutCreateInfo plci = vkinit::pipeline_layout_create_info();
	VkDescriptorSetLayout setLayouts[] = { terrainBrushDescriptorLayout };
	plci.setLayoutCount = 1;
	plci.pSetLayouts = setLayouts;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pc;

	VK_CHECK(vkCreatePipelineLayout(engine.device, &plci, nullptr, &terrainBrushCompute.pLayout));

	// compute pipeline
	VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = cs;
	stage.pName = "main";

	VkComputePipelineCreateInfo ci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	ci.stage = stage;
	ci.layout = terrainBrushCompute.pLayout;

	VK_CHECK(vkCreateComputePipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &terrainBrushCompute.pipeline));

	vkDestroyShaderModule(engine.device, cs, nullptr);

	engine.mainDeletionQueue.push_pipeline(terrainBrushCompute.pipeline);
	engine.mainDeletionQueue.push_pipeline_layout(terrainBrushCompute.pLayout);
}

void Renderer::upload_terrain_instances(const TerrainInstance* data, uint32_t count) {
	void* p = nullptr;
	vmaMapMemory(engine.vmaAllocator, terrainInstanceBuffer.allocation, &p);
	std::memcpy(p, data, sizeof(TerrainInstance) * count);  
	vmaUnmapMemory(engine.vmaAllocator, terrainInstanceBuffer.allocation);
}

void Renderer::update_brush_ubo(const glm::vec2& centerWorldXZ, float radiusMeters, float strength, int mode)
{
	BrushUBO b{};
	b.centerXZ = centerWorldXZ;
	b.radiusM = radiusMeters;
	b.edgeM = 0.5f;                 // keep your soft edge
	b.color = { 1.f, 0.f, 0.f, 1.f };
	b.opacity = 0.6f;

	// NEW: keep CPU & GPU in sync
	b.strength = strength;
	b.mode = mode;

	void* data;
	vmaMapMemory(engine.vmaAllocator, brushUBO.allocation, &data);
	memcpy(data, &b, sizeof(BrushUBO));
	vmaUnmapMemory(engine.vmaAllocator, brushUBO.allocation);
}

void Renderer::init_imgui() {

	VkDescriptorPoolSize pool_sizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE },
	};
	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 0;
	for (VkDescriptorPoolSize& pool_size : pool_sizes)
		pool_info.maxSets += pool_size.descriptorCount;
	pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;



	VK_CHECK(vkCreateDescriptorPool(engine.device, &pool_info, nullptr, &imguiPool));


	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

	engine.main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(engine.main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = engine.main_scale;

	ImGui_ImplSDL3_InitForVulkan(engine.window);
	ImGui_ImplVulkan_InitInfo init_info = {};
	//init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
	init_info.Instance = engine.instance;
	init_info.PhysicalDevice = engine.physicalDevice;
	init_info.Device = engine.device;
	init_info.QueueFamily = engine.graphicsQueueFamily;
	init_info.Queue = engine.graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = engine.swapchainImageCount;
	init_info.ImageCount = engine.swapchainImageCount;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.Allocator = engine.vkAllocator;
	init_info.PipelineCache = VK_NULL_HANDLE;
	//classic rendering
	init_info.RenderPass = swapchainRenderPass;
	init_info.Subpass = 0;

	//dynamic rendering info below 
	//init_info.UseDynamicRendering = true;
	//init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	//init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	//init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainImageFormat;
	ImGui_ImplVulkan_Init(&init_info);
	

	engine.mainDeletionQueue.push_descriptor_pool(imguiPool);
}

void Renderer::init_default_data() {

	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	whiteImage = create_image((void*)&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
	greyImage = create_image((void*)&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
	blackImage = create_image((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	std::array<uint32_t, 16 * 16 > pixels; //for 16x16 checkerboard texture
	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 16; y++) {
			pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
		}
	}
	errorCheckerBoardImage = create_image(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	VkSamplerCreateInfo samplInfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplInfo.magFilter = VK_FILTER_NEAREST;
	samplInfo.minFilter = VK_FILTER_NEAREST;

	vkCreateSampler(engine.device, &samplInfo, engine.vkAllocator, &defaultSamplerNearest);

	samplInfo.magFilter = VK_FILTER_LINEAR;
	samplInfo.minFilter = VK_FILTER_LINEAR;
	vkCreateSampler(engine.device, &samplInfo, engine.vkAllocator, &defaultSamplerLinear);


	engine.mainDeletionQueue.push_sampler(defaultSamplerNearest);
	engine.mainDeletionQueue.push_sampler(defaultSamplerLinear);
	engine.mainDeletionQueue.push_allocated_image(whiteImage);
	engine.mainDeletionQueue.push_allocated_image(blackImage);
	engine.mainDeletionQueue.push_allocated_image(greyImage);
	engine.mainDeletionQueue.push_allocated_image(errorCheckerBoardImage);
	engine.mainDeletionQueue.push_mesh_buffer_deletion(rectangle);


	GLTFMetallic_Roughness::MaterialResources materialResources{};
	materialResources.colorImage = whiteImage;
	materialResources.colorSampler = defaultSamplerLinear;
	materialResources.metalRoughImage = whiteImage;
	materialResources.metalRoughSampler = defaultSamplerLinear;

	AllocatedBuffer materialConstants =
		engine.create_buffer(sizeof(GLTFMetallic_Roughness::MaterialConstants),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU);

	void* mapped = nullptr;
	vmaMapMemory(engine.vmaAllocator, materialConstants.allocation, &mapped);
	auto* matUBO = reinterpret_cast<GLTFMetallic_Roughness::MaterialConstants*>(mapped);

	*matUBO = {};
	matUBO->colorFactors = glm::vec4(1.0f);         
	matUBO->metal_rough_factors = glm::vec4(1.0f, 0.5f, 0, 0); 

	vmaUnmapMemory(engine.vmaAllocator, materialConstants.allocation);

	engine.mainDeletionQueue.push_allocated_buffer(materialConstants);

	materialResources.dataBuffer = materialConstants.buffer;
	materialResources.dataBufferOffset = 0;

	materialData = metalRoughMaterial.write_material(engine.device,
		MaterialPass::MainColor, materialResources, globalDescriptorAllocator, this);

}

void Renderer::terrain_build_indirect(VkCommandBuffer cmd)
{
	// reset count
	vkCmdFillBuffer(cmd, terrainDrawCount.buffer, 0, sizeof(uint32_t), 0);

	// TRANSFER -> COMPUTE (for the count clear)
	VkBufferMemoryBarrier2 b0{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b0.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	b0.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	b0.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b0.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
	b0.buffer = terrainDrawCount.buffer; b0.offset = 0; b0.size = sizeof(uint32_t);
	VkDependencyInfo dep0{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dep0.bufferMemoryBarrierCount = 1; dep0.pBufferMemoryBarriers = &b0;
	vkCmdPipelineBarrier2(cmd, &dep0);

	// dispatch compute
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, terrainCompute.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, terrainCompute.pLayout, 0, 1, &terrainBuildSet, 0, nullptr);

	PC_TerrainBuild pc{};
	pc.firstIndex = 0;
	pc.indexCount = terrainGrid.indexCountTotal;
	pc.vertexOffset = 0;
	pc.tileCount = terrainInstanceCount;

	vkCmdPushConstants(cmd, terrainCompute.pLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
	uint32_t groups = (terrainInstanceCount + 63) / 64;
	vkCmdDispatch(cmd, groups, 1, 1);

	// COMPUTE -> DRAW_INDIRECT (for draws + count)
	VkBufferMemoryBarrier2 b1{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b1.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b1.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b1.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
	b1.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
	b1.buffer = terrainIndirectDraws.buffer; b1.offset = 0; b1.size = VK_WHOLE_SIZE;

	VkBufferMemoryBarrier2 b2 = b1; b2.buffer = terrainDrawCount.buffer; b2.size = sizeof(uint32_t);

	VkDependencyInfo dep1{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	VkBufferMemoryBarrier2 arr[]{ b1, b2 };
	dep1.bufferMemoryBarrierCount = 2; dep1.pBufferMemoryBarriers = arr;
	vkCmdPipelineBarrier2(cmd, &dep1);
	terrainIndirectBuiltThisFrame = true; 
}



void Renderer::gpu_draw(VkCommandBuffer cmd, const RenderContext& context) {
	// must have been produced by compute this frame
	if (!terrainIndirectBuiltThisFrame) return;

	VkPipeline       gp = newManager.getPipeline(pidHeightMap);
	VkPipelineLayout gpl = newManager.getLayout(pidHeightMap);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gp);

	if (context.globalSet) {
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gpl, 0, 1, &context.globalSet, 0, nullptr);
	}
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gpl, 1, 1, &terrainSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gpl, 2, 1, &heightmapTexSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gpl, 3, 1, &heightmapDiffuseSet, 0, nullptr);

	GPUDrawPushConstants pc{};
	pc.worldMatrix = glm::mat4(1.0f);
	pc.vertexBuffer = terrainGrid.vertexBufferAddress;


	vkCmdPushConstants(cmd, gpl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pc);


	vkCmdBindIndexBuffer(cmd, terrainGrid.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	auto fn = (PFN_vkCmdDrawIndexedIndirectCount)
		vkGetDeviceProcAddr(engine.device, "vkCmdDrawIndexedIndirectCount");
	if (!fn) return; // **no fallback path**

	fn(cmd,
		terrainIndirectDraws.buffer, 0,
		terrainDrawCount.buffer, 0,
		terrainInstanceCount,
		sizeof(VkDrawIndexedIndirectCommand));

	// consume-once; force compute to rebuild next frame
	terrainIndirectBuiltThisFrame = false;
}


void Renderer::cpu_draw(VkCommandBuffer cmd, VkPipelineLayout currentLayout, const RenderItem& item) {

	GPUDrawPushConstants pushConstants;

	pushConstants.worldMatrix = item.gpuData.transform;
	pushConstants.vertexBuffer = item.gpuData.vertexBufferAddress;

	vkCmdPushConstants(cmd, currentLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
	vkCmdBindIndexBuffer(cmd, item.gpuData.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd,
		item.gpuData.indexCount,
		item.gpuData.instanceCount ? item.gpuData.instanceCount : 1, // guard
		item.gpuData.firstIndex,
		0, 0);
}

void Renderer::render_pass_geometry(VkCommandBuffer cmd, const std::vector<RenderItem>& items, const RenderContext& context){
	VkViewport viewport{};
	viewport.x = 0; viewport.y = 0;
	viewport.width = drawExtent.width;
	viewport.height = drawExtent.height;
	viewport.minDepth = 0.f; viewport.maxDepth = 1.f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset = { 0,0 };
	scissor.extent = { (uint32_t)viewport.width, (uint32_t)viewport.height };
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	std::vector<RenderItem> itemsIn = items;
	std::stable_sort(itemsIn.begin(), itemsIn.end(),
		[](const RenderItem& a, const RenderItem& b) { return a.pID < b.pID; });

	PID currentPID = std::numeric_limits<PID>::max();
	VkPipelineLayout currentLayout = VK_NULL_HANDLE;

	for (auto& ri : itemsIn) {

		if (ri.pID == pidHeightMap) continue;

		if (ri.pID != currentPID) {
			currentPID = ri.pID;

			VkPipeline p = newManager.getPipeline(currentPID);
			VkPipelineLayout pl = newManager.getLayout(currentPID);
			if (p == VK_NULL_HANDLE || pl == VK_NULL_HANDLE) continue;

			currentLayout = pl;
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);

			if (context.globalSet != VK_NULL_HANDLE) {
				const VkDescriptorSet globalSet = context.globalSet;
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					currentLayout, 0, 1, &globalSet, 0, nullptr);
			}
		}

		DescriptorUtil::bindBundle(cmd, currentLayout, ri.desc);
		cpu_draw(cmd, currentLayout, ri);
	}

	gpu_draw(cmd, context);
}

void Renderer::render_imgui(VkCommandBuffer cmd) {
	//classic renderpass
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void Renderer::render_dynamic_imgui(VkCommandBuffer cmd, VkImageView targetImageView) {
	//inactive
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkinit::rendering_info(engine.swapchainExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}



void Renderer::create_draw_image_renderpass() {
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = engine.drawImage.imageFormat; // VK_FORMAT_R16G16B16A16_SFLOAT
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // we load because we get an image from background pipelines, do clear if we dont render from background anymore
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;


	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = engine.depthImage.imageFormat;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	dependency.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = (uint32_t)attachments.size();
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	VK_CHECK(vkCreateRenderPass(engine.device, &renderPassInfo, nullptr, &drawImageRenderPass));

	engine.mainDeletionQueue.push_renderpass(drawImageRenderPass);
}

void Renderer::create_swapchain_renderpass() {
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = engine.swapchainImageFormat; // e.g., VK_FORMAT_B8G8R8A8_UNORM
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Preserve copied data
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;


	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT; // Wait for vkCmdCopyImage
	dependency.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // Copy writes
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	VK_CHECK(vkCreateRenderPass(engine.device, &renderPassInfo, nullptr, &swapchainRenderPass));


	engine.mainDeletionQueue.push_renderpass(swapchainRenderPass);

}

void Renderer::create_swapchain_framebuffer() {


	// Resize the framebuffers vector to match the number of swapchain images
	swapchainFrameBuffers.resize(engine.swapchainImages.size());

	// Create a framebuffer for each swapchain image
	for (size_t i = 0; i < engine.swapchainImages.size(); ++i) {
		// Use the image view from the swapchain
		VkImageView attachments[] = { engine.swapchainImageViews[i] }; // Assuming swapchainImageViews is a std::vector<VkImageView>

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = swapchainRenderPass; // Reference the render pass
		framebufferInfo.attachmentCount = 1; // One color attachment
		framebufferInfo.pAttachments = attachments;
		framebufferInfo.width = engine.swapchainExtent.width; // From swapchain creation
		framebufferInfo.height = engine.swapchainExtent.height;
		framebufferInfo.layers = 1; // Single layer

		VK_CHECK(vkCreateFramebuffer(engine.device, &framebufferInfo, nullptr, &swapchainFrameBuffers[i]));

		engine.mainDeletionQueue.push_framebuffer(swapchainFrameBuffers[i]);
		
	}
}

void Renderer::create_draw_image_framebuffer() {
	std::array<VkImageView, 2> attachments = { engine.drawImage.imageView, engine.depthImage.imageView }; // VkImageView for drawImage.image

	VkFramebufferCreateInfo framebufferInfo = {};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = drawImageRenderPass; // Reference the drawImage render pass
	framebufferInfo.attachmentCount = (uint32_t)attachments.size(); 
	framebufferInfo.pAttachments = attachments.data();
	framebufferInfo.width = engine.drawImage.imageExtent.width; // From drawImage creation
	framebufferInfo.height = engine.drawImage.imageExtent.height;
	framebufferInfo.layers = 1; // Single layer

	VK_CHECK(vkCreateFramebuffer(engine.device, &framebufferInfo, nullptr, &drawImageFrameBuffer));

	engine.mainDeletionQueue.push_framebuffer(drawImageFrameBuffer);
}

AllocatedImage Renderer::create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {

	AllocatedImage newImage;
	newImage.imageFormat = format;
	newImage.imageExtent = size;

	VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
	if (mipmapped) {
		img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
	}

	// always allocate images on dedicated GPU memory
	VmaAllocationCreateInfo allocinfo = {};
	allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// allocate and create the image
	VK_CHECK(vmaCreateImage(engine.vmaAllocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

	// if the format is a depth format, we will need to have it use the correct
	// aspect flag
	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT) {
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	// build a image-view for the image
	VkImageViewCreateInfo view_info = vkinit::imageview_create_info(format, newImage.image, aspectFlag);
	view_info.subresourceRange.levelCount = img_info.mipLevels;

	VK_CHECK(vkCreateImageView(engine.device, &view_info, nullptr, &newImage.imageView));

	return newImage;

}


static inline uint32_t bytes_per_pixel(VkFormat f) {
	switch (f) {
	case VK_FORMAT_R8_UNORM:                return 1;
	case VK_FORMAT_R16_UNORM:               return 2;
	case VK_FORMAT_R8G8B8A8_UNORM:          return 4;
	case VK_FORMAT_R16G16B16A16_UNORM:      return 8;
	case VK_FORMAT_R16_SFLOAT:              return 2;
	case VK_FORMAT_R16G16B16A16_SFLOAT:     return 8;
	case VK_FORMAT_R32_SFLOAT:              return 4;
	case VK_FORMAT_R32G32B32A32_SFLOAT:     return 16;
	default:                                return 4; // fallback
	}
}

static inline VkImageAspectFlags aspect_for_format(VkFormat f) {
	switch (f) {
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}


AllocatedImage Renderer::create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
	const uint32_t bpp = bytes_per_pixel(format);
	const VkDeviceSize data_size =
		VkDeviceSize(size.width) * size.height * size.depth * bpp;

	AllocatedBuffer uploadbuffer =
		engine.create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU);

	std::memcpy(uploadbuffer.info.pMappedData, data, size_t(data_size));

	// include TRANSFER_SRC only if you’ll generate mips
	VkImageUsageFlags imgUsage = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		(mipmapped ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);

	AllocatedImage new_image = create_image(size, format, imgUsage, mipmapped);

	engine.immediateCommandSubmit([&](VkCommandBuffer cmd) {
		vkutil::transition_image(cmd, new_image.image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion{};
		copyRegion.imageSubresource.aspectMask = aspect_for_format(format);
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = size;

		vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, new_image.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		if (mipmapped) {
			vkutil::generate_mipmaps(cmd, new_image.image,
				VkExtent2D{ new_image.imageExtent.width, new_image.imageExtent.height });
		}
		else {
			vkutil::transition_image(cmd, new_image.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		});

	vmaDestroyBuffer(engine.vmaAllocator, uploadbuffer.buffer, uploadbuffer.allocation);
	return new_image;
}




AllocatedImage Renderer::create_image_array(VkExtent3D size, uint32_t layers, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
	AllocatedImage img{};
	img.imageFormat = format;
	img.imageExtent = size;

	VkImageCreateInfo ci = vkinit::image_create_info(format, usage, size);
	ci.arrayLayers = layers;                      // <-- array
	ci.imageType = VK_IMAGE_TYPE_2D;
	ci.samples = VK_SAMPLE_COUNT_1_BIT;
	ci.tiling = VK_IMAGE_TILING_OPTIMAL;
	if (mipmapped) {
		ci.mipLevels = uint32_t(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
	}

	VmaAllocationCreateInfo aci{};
	aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	aci.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VK_CHECK(vmaCreateImage(engine.vmaAllocator, &ci, &aci, &img.image, &img.allocation, nullptr));

	VkImageAspectFlags aspect = aspect_for_format(format);

	VkImageViewCreateInfo vi = vkinit::imageview_create_info(format, img.image, aspect);
	vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;    // <-- array view
	vi.subresourceRange.levelCount = ci.mipLevels;
	vi.subresourceRange.baseArrayLayer = 0;
	vi.subresourceRange.layerCount = layers;      // <-- all layers

	VK_CHECK(vkCreateImageView(engine.device, &vi, nullptr, &img.imageView));
	return img;
}

void Renderer::upload_to_image_array_layer(AllocatedImage& dstArray, uint32_t layer, void* data, VkExtent3D size, VkFormat format, bool mipmapped) {
	const uint32_t bpp = bytes_per_pixel(format);
	const VkDeviceSize dataSize = VkDeviceSize(size.width) * size.height * size.depth * bpp;

	AllocatedBuffer staging = engine.create_buffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	std::memcpy(staging.info.pMappedData, data, size_t(dataSize));

	engine.immediateCommandSubmit([&](VkCommandBuffer cmd) {
		// transition target layer to TRANSFER_DST
		VkImageSubresourceRange range{};
		range.aspectMask = aspect_for_format(format);
		range.baseMipLevel = 0;
		range.levelCount = mipmapped ? (uint32_t)(std::floor(std::log2(std::max(size.width, size.height))) + 1) : 1;
		range.baseArrayLayer = layer;              // <-- only this slice
		range.layerCount = 1;

		vkutil::transition_image(cmd, dstArray.image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);

		VkBufferImageCopy copy{};
		copy.imageSubresource.aspectMask = range.aspectMask;
		copy.imageSubresource.mipLevel = 0;
		copy.imageSubresource.baseArrayLayer = layer; // <-- write this slice
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent = size;

		vkCmdCopyBufferToImage(cmd, staging.buffer, dstArray.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

		if (mipmapped) {
			// generate mips for this slice only
			vkutil::generate_mipmaps(cmd, dstArray.image, VkExtent2D{ size.width, size.height }, layer, /*layerCount*/1);
		}
		else {
			// transition to shader-read for this slice
			vkutil::transition_image(cmd, dstArray.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);
		}
		});

	vmaDestroyBuffer(engine.vmaAllocator, staging.buffer, staging.allocation);
}

void Renderer::clear_image_array_layer(AllocatedImage& dstArray, uint32_t layer, VkFormat format, float value/*0..1*/) {
	
	engine.immediateCommandSubmit([&](VkCommandBuffer cmd) {
		VkImageSubresourceRange range{};
		range.aspectMask = aspect_for_format(format);
		range.baseMipLevel = 0;
		range.levelCount = VK_REMAINING_MIP_LEVELS;
		range.baseArrayLayer = layer;
		range.layerCount = 1;

		vkutil::transition_image(cmd, dstArray.image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, range);

		VkClearColorValue cc{};
		cc.float32[0] = value; cc.float32[1] = 0; cc.float32[2] = 0; cc.float32[3] = 1;

		vkCmdClearColorImage(cmd, dstArray.image, VK_IMAGE_LAYOUT_GENERAL, &cc, 1, &range);

		vkutil::transition_image(cmd, dstArray.image,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);
		});
}



void Renderer::HotloadShader() {

	auto& shaderMap = newManager.get_shaderMap();
	std::set<PipelineRes*> pipelinesToRebuild;

	// --- detect only (no mutation) ---
	for (auto& [file, resources] : shaderMap) {
		const auto newStamp = ShaderUtil::getFileTimeStamp(file);

		for (auto* r : resources) {
			if (file == r->shaderType.vertexShader.file.path) {
				fmt::print("Checking file: {} old: {} new: {}\n", file,
					r->shaderType.vertexShader.lastModified.time_since_epoch().count(),
					newStamp.time_since_epoch().count());
				if (newStamp != r->shaderType.vertexShader.lastModified)
					pipelinesToRebuild.insert(r);
			}
			if (file == r->shaderType.fragmentShader.file.path) {
				fmt::print("Checking file: {} old: {} new: {}\n", file,
					r->shaderType.fragmentShader.lastModified.time_since_epoch().count(),
					newStamp.time_since_epoch().count());
				if (newStamp != r->shaderType.fragmentShader.lastModified)
					pipelinesToRebuild.insert(r);
			}
			if (file == r->shaderType.geometryShader.file.path) {
				fmt::print("Checking file: {} old: {} new: {}\n", file,
					r->shaderType.geometryShader.lastModified.time_since_epoch().count(),
					newStamp.time_since_epoch().count());
				if (newStamp != r->shaderType.geometryShader.lastModified)
					pipelinesToRebuild.insert(r);
			}
			if (file == r->shaderType.computeShader.file.path) {
				fmt::print("Checking file: {} old: {} new: {}\n", file,
					r->shaderType.computeShader.lastModified.time_since_epoch().count(),
					newStamp.time_since_epoch().count());
				if (newStamp != r->shaderType.computeShader.lastModified)
					pipelinesToRebuild.insert(r);
			}
		}
	}

	for (auto* r : pipelinesToRebuild) {
		VkPipeline rebuilt = rebuild(engine.device, *r);
		if (rebuilt == VK_NULL_HANDLE) {
			fmt::print("Rebuild failed for pipeline={} name={}\n",
				(void*)r->pipeline, r->name ? r->name : "(unnamed)");
			continue;
		}

		engine.mainDeletionQueue.push_pipeline(rebuilt);

		if (!r->shaderType.vertexShader.file.path.empty())
			r->shaderType.vertexShader.lastModified =
			ShaderUtil::getFileTimeStamp(r->shaderType.vertexShader.file.path);
		if (!r->shaderType.fragmentShader.file.path.empty())
			r->shaderType.fragmentShader.lastModified =
			ShaderUtil::getFileTimeStamp(r->shaderType.fragmentShader.file.path);
		if (!r->shaderType.geometryShader.file.path.empty())
			r->shaderType.geometryShader.lastModified =
			ShaderUtil::getFileTimeStamp(r->shaderType.geometryShader.file.path);
		if (!r->shaderType.computeShader.file.path.empty())
			r->shaderType.computeShader.lastModified =
			ShaderUtil::getFileTimeStamp(r->shaderType.computeShader.file.path);
	}

}

VkPipeline Renderer::rebuild(VkDevice device, PipelineRes& res) {
	fmt::print("rebuildPipelines called\n");

	VkPipeline oldPipeline = res.pipeline;
	auto* resConfig = res.getGraphicsConfig();

	fmt::print("old pipeline object identification is {}\n ", (void*)oldPipeline);
	fmt::print("RenderPass handle on rebuild: {}\n", (void*)resConfig->renderPass);

	resConfig->shaderStages.clear();

	VkShaderModule vertexModule = VK_NULL_HANDLE;
	VkShaderModule fragmentModule = VK_NULL_HANDLE;

	if (!res.shaderType.vertexShader.file.path.empty()) {
		vertexModule = ShaderUtil::compileToSPV(device, res.shaderType.vertexShader.file.path, EShLangVertex);
		resConfig->shaderStages.push_back(
			vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexModule));
	}
	if (!res.shaderType.fragmentShader.file.path.empty()) {
		fragmentModule = ShaderUtil::compileToSPV(device, res.shaderType.fragmentShader.file.path, EShLangFragment);
		resConfig->shaderStages.push_back(
			vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule));
	}

	VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

	if (resConfig->renderMode == RenderMode::Dynamic) {
		pipelineInfo.renderPass = VK_NULL_HANDLE;
		pipelineInfo.subpass = 0;
		pipelineInfo.pNext = &resConfig->renderInfo;
		resConfig->renderPass = VK_NULL_HANDLE;
	}
	else {
		pipelineInfo.renderPass = resConfig->renderPass;
		pipelineInfo.subpass = 0;
		pipelineInfo.pNext = nullptr;
	}

	pipelineInfo.stageCount = (uint32_t)resConfig->shaderStages.size();
	pipelineInfo.pStages = resConfig->shaderStages.data();
	pipelineInfo.pVertexInputState = &resConfig->vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &resConfig->inputAssembly;
	pipelineInfo.pViewportState = &resConfig->viewportStateInfo;
	pipelineInfo.pRasterizationState = &resConfig->rasterizer;
	pipelineInfo.pMultisampleState = &resConfig->multisampling;
	pipelineInfo.pColorBlendState = &resConfig->colorBlendingInfo;
	pipelineInfo.pDepthStencilState = &resConfig->depthStencil;
	pipelineInfo.pDynamicState = &resConfig->dynamicStateInfo;
	pipelineInfo.layout = res.pLayout;

	fmt::print("Pipeline pointers:\n");
	fmt::print("  pStages: {}\n", (void*)pipelineInfo.pStages);
	fmt::print("  pVertexInputState: {}\n", (void*)pipelineInfo.pVertexInputState);
	fmt::print("  pInputAssemblyState: {}\n", (void*)pipelineInfo.pInputAssemblyState);
	fmt::print("  pViewportState: {}\n", (void*)pipelineInfo.pViewportState);
	fmt::print("  pRasterizationState: {}\n", (void*)pipelineInfo.pRasterizationState);
	fmt::print("  pMultisampleState: {}\n", (void*)pipelineInfo.pMultisampleState);
	fmt::print("  pColorBlendState: {}\n", (void*)pipelineInfo.pColorBlendState);
	fmt::print("  pDepthStencilState: {}\n", (void*)pipelineInfo.pDepthStencilState);
	fmt::print("  layout: {}\n", (void*)pipelineInfo.layout);
	fmt::print("  pDynamicState: {}\n", (void*)pipelineInfo.pDynamicState);
	fmt::print("  renderPass: {}\n", (void*)pipelineInfo.renderPass);

	VkPipeline newPipeline = VK_NULL_HANDLE;
	VkResult vr = vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineInfo, nullptr, &newPipeline);

	if (vertexModule)   vkDestroyShaderModule(device, vertexModule, nullptr);
	if (fragmentModule) vkDestroyShaderModule(device, fragmentModule, nullptr);

	if (vr != VK_SUCCESS) {
		fmt::println("Failed to rebuild pipeline");
		return VK_NULL_HANDLE;
	}

	res.pipeline = newPipeline;
	return newPipeline;
}


void GLTFMetallic_Roughness::build_pipelines(VulkanEngine* engine, Renderer* renderer) {

	auto& newOpaquePipeline = renderer->newOpaquePipeline;
	auto& newTransparentPipeline = renderer->newTransparentPipeline;

	newOpaquePipeline.type = PipelineType::Graphics;
	newTransparentPipeline.type = PipelineType::Graphics;

	newOpaquePipeline.shaderType.vertexShader.file = { "mesh.vert" };
	newOpaquePipeline.shaderType.fragmentShader.file = { "mesh.frag" };

	newOpaquePipeline.shaderType.vertexShader.lastModified =
		ShaderUtil::getFileTimeStamp(newOpaquePipeline.shaderType.vertexShader.file.path);
	newOpaquePipeline.shaderType.fragmentShader.lastModified =
		ShaderUtil::getFileTimeStamp(newOpaquePipeline.shaderType.fragmentShader.file.path);

	VkShaderModule meshVertShader =
		ShaderUtil::compileToSPV(engine->device, newOpaquePipeline.shaderType.vertexShader.file.path, EShLangVertex);
	VkShaderModule meshFragShader =
		ShaderUtil::compileToSPV(engine->device, newOpaquePipeline.shaderType.fragmentShader.file.path, EShLangFragment);

	auto* config = newOpaquePipeline.getGraphicsConfig();
	config->pushConstantRange.offset = 0;
	config->pushConstantRange.size = sizeof(GPUDrawPushConstants);
	config->pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	DescriptorLayoutBuilder layoutBuilder;
	layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	layoutBuilder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	materialLayout = layoutBuilder.build(
		engine->device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

	VkDescriptorSetLayout setLayouts[] = { renderer->gpuSceneDataDescriptorLayout, materialLayout };

	config->layoutInfo = vkinit::pipeline_layout_create_info();
	config->layoutInfo.setLayoutCount = 2;
	config->layoutInfo.pSetLayouts = setLayouts;
	config->layoutInfo.pPushConstantRanges = &config->pushConstantRange;
	config->layoutInfo.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(
		engine->device, &config->layoutInfo, engine->vkAllocator, &newOpaquePipeline.pLayout));


	VK_CHECK(vkCreatePipelineLayout(
		engine->device, &config->layoutInfo, engine->vkAllocator, &newTransparentPipeline.pLayout));

	PipelineBuilder builder;
	builder.set_shaders(meshVertShader, meshFragShader);
	builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	builder.set_multisampling_none();

	builder.disable_blending();
	builder.enable_depthtest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
	builder.set_renderpass(renderer->drawImageRenderPass);
	builder.res->pLayout = newOpaquePipeline.pLayout;
	newOpaquePipeline.pipeline = builder.build_pipeline(engine->device, RenderMode::Classic, &newOpaquePipeline);

	builder.enable_blending_additive();
	builder.enable_depthtest(false, VK_COMPARE_OP_LESS_OR_EQUAL);
	builder.res->pLayout = newTransparentPipeline.pLayout;
	newTransparentPipeline.pipeline = builder.build_pipeline(engine->device, RenderMode::Classic, &newTransparentPipeline);

	vkDestroyShaderModule(engine->device, meshVertShader, nullptr);
	vkDestroyShaderModule(engine->device, meshFragShader, nullptr);

	// Persist handles into your pipeline structs (so bind/push use the right layout)
	


	renderer->pidNewOpaquePipeline = renderer->newManager.registerPipeline(newOpaquePipeline, Hotloadable::True);
	renderer->pidNewTransparentPipeline = renderer->newManager.registerPipeline(newTransparentPipeline, Hotloadable::True);
	renderer->newManager.showInfo();


	engine->mainDeletionQueue.push_descriptor_set_layout(materialLayout);

}

MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator, Renderer* renderer) {

	auto& newOpaquePipeline = renderer->newOpaquePipeline;
	auto& newTransparentPipeline = renderer->newTransparentPipeline;

	MaterialInstance matData;
	matData.passType = pass;

	if (pass == MaterialPass::Transparent) {
		matData.pipeline = &newTransparentPipeline.pipeline;
	}
	else {
		matData.pipeline = &newOpaquePipeline.pipeline;
	}
	
	DescriptorWriter w;
	w.write_buffer(0, resources.dataBuffer, sizeof(MaterialConstants),
		resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	w.write_image(1, resources.colorImage.imageView, resources.colorSampler,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	w.write_image(2, resources.metalRoughImage.imageView, resources.metalRoughSampler,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	matData.materialSet = DescriptorUtil::buildDescriptorSet(device, materialLayout, descriptorAllocator, w);

	return matData;
}


TextureID TextureCache::AddTexture(const VkImageView& image, VkSampler sampler)
{
	for (unsigned int i = 0; i < Cache.size(); i++) {
		if (Cache[i].imageView == image && Cache[i].sampler == sampler) {
			//found, return it
			return TextureID{ i };
		}
	}

	uint32_t idx = Cache.size();

	Cache.push_back(VkDescriptorImageInfo{ .sampler = sampler,.imageView = image, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

	return TextureID{ idx };
}

// compute (inclusive) texel AABB in tile-local texel space
static inline bool brush_rect_for_tile(const BrushStroke& b, uint32_t H,
	int& x0, int& y0, int& x1, int& y1,
	float& cx, float& cy, float& rTex)
{
	const glm::vec2 centerLocal = (b.worldXZ - b.tileOrigin) / b.worldScale; // texels
	cx = centerLocal.x; cy = centerLocal.y;
	rTex = b.radiusWorld / b.worldScale;

	x0 = int(std::floor(cx - rTex - 1.f));
	y0 = int(std::floor(cy - rTex - 1.f));
	x1 = int(std::ceil(cx + rTex + 1.f));
	y1 = int(std::ceil(cy + rTex + 1.f));

	x0 = std::max(0, x0); y0 = std::max(0, y0);
	x1 = std::min<int>(int(H) - 1, x1);
	y1 = std::min<int>(int(H) - 1, y1);

	return (x1 >= x0) && (y1 >= y0);
}

void Renderer::gpu_brush_apply(VkCommandBuffer cmd, const BrushStroke& b)
{
	const uint32_t H = heightmapImage.imageExtent.width;
	const uint32_t mipCount = uint32_t(std::floor(std::log2(H))) + 1; // add this line

	int x0, y0, x1, y1; float cx, cy, rTex;
	if (!brush_rect_for_tile(b, H, x0, y0, x1, y1, cx, cy, rTex)) return;

	const uint32_t W = uint32_t(x1 - x0 + 1);
	const uint32_t K = uint32_t(y1 - y0 + 1);
	if (W == 0 || K == 0) return;

	// SRV -> GENERAL (whole layer: all mips)
	{
		VkImageMemoryBarrier2 ib{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		ib.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		ib.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		ib.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		ib.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
		ib.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		ib.image = heightmapImage.image;
		ib.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, b.layer, 1 }; // <<< levelCount=mipCount

		VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &ib;
		vkCmdPipelineBarrier2(cmd, &dep);
	}

	// bind + push + dispatch (unchanged)
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, terrainBrushCompute.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, terrainBrushCompute.pLayout, 0, 1, &terrainBrushSet, 0, nullptr);

	struct PC_Brush {
		uint32_t layer; int32_t x0, y0; float cx, cy; float rTex; float strength; int32_t mode;
	} pc;
	pc.layer = b.layer; pc.x0 = x0; pc.y0 = y0; pc.cx = cx; pc.cy = cy; pc.rTex = rTex; pc.strength = b.strength / heightAmplitudeMeters, pc.mode = b.mode;

	const uint32_t lx = 16, ly = 16;
	vkCmdPushConstants(cmd, terrainBrushCompute.pLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PC_Brush), &pc);
	vkCmdDispatch(cmd, (W + lx - 1) / lx, (K + ly - 1) / ly, 1);

	// GENERAL -> SRV (whole layer: all mips)
	{
		VkImageMemoryBarrier2 ib{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		ib.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		ib.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
		ib.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		ib.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		ib.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		ib.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // <<< return to SRV
		ib.image = heightmapImage.image;
		ib.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, b.layer, 1 }; // <<< levelCount=mipCount

		VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &ib;
		vkCmdPipelineBarrier2(cmd, &dep);
	}
}


// REQUIREMENTS:
// - renderer->heightmapImage: array image, VK_FORMAT_R16_SFLOAT, with TRANSFER_SRC/DST usage
// - engine.vmaAllocator, vkutil::create_staging_buffer, f16<->f32 helpers
// - call this between upload and geometry pass (so final barrier lands before sampling)

void Renderer::cpu_brush_small(VkCommandBuffer cmd, const BrushStroke& b)
{
	const uint32_t H = heightmapImage.imageExtent.width; // square tile
	int x0, y0, x1, y1; float cx, cy, rTex;
	if (!brush_rect_for_tile(b, H, x0, y0, x1, y1, cx, cy, rTex)) return;

	const uint32_t W = uint32_t(x1 - x0 + 1);
	const uint32_t K = uint32_t(y1 - y0 + 1);
	const VkDeviceSize bytes = VkDeviceSize(W) * VkDeviceSize(K) * sizeof(uint16_t);

	AllocatedBuffer stage = engine.create_buffer(
		bytes,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
	);
	engine.mainDeletionQueue.push_allocated_buffer(stage);

	// determine if allocation is HOST_COHERENT
	VkMemoryPropertyFlags memProps = 0;
	vmaGetAllocationMemoryProperties(engine.vmaAllocator, stage.allocation, &memProps);
	const bool isCoherent = (memProps & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

	// make image accessible for TRANSFER
	{
		VkImageMemoryBarrier2 ib{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		ib.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		ib.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
		ib.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		ib.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
		ib.oldLayout = VK_IMAGE_LAYOUT_GENERAL; // keep GENERAL during editing
		ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		ib.image = heightmapImage.image;
		ib.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, b.layer, 1 };

		VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &ib;
		vkCmdPipelineBarrier2(cmd, &dep);
	}

	// copy FROM image region → staging
	{
		VkBufferImageCopy r{};
		r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, b.layer, 1 };
		r.imageOffset = { x0, y0, 0 };
		r.imageExtent = { W, K, 1 };
		vkCmdCopyImageToBuffer(cmd, heightmapImage.image, VK_IMAGE_LAYOUT_GENERAL, stage.buffer, 1, &r);
	}

	// NOTE: Submit this cmd buffer and wait on a fence BEFORE CPU reads (host sync point).
	// After the wait, if not HOST_COHERENT, invalidate the mapped range so CPU sees latest data.
	if (!isCoherent) {
		vmaInvalidateAllocation(engine.vmaAllocator, stage.allocation, 0, bytes);
	}

	// CPU modify (map → edit → [flush if needed] → unmap)
	{
		void* p = nullptr;
		vmaMapMemory(engine.vmaAllocator, stage.allocation, &p);
		auto* h16 = reinterpret_cast<uint16_t*>(p);

		const float r2 = rTex * rTex;
		for (uint32_t j = 0; j < K; ++j) {
			const int ty = y0 + int(j);
			const float dy = float(ty) - cy;
			for (uint32_t i = 0; i < W; ++i) {
				const int tx = x0 + int(i);
				const float dx = float(tx) - cx;
				const float d2 = dx * dx + dy * dy;
				if (d2 > r2) continue;

				float h = vkutil::f16_to_f32(h16[j * W + i]);
				float w = 1.f - (d2 / r2); // simple falloff
				w = glm::clamp(w, 0.f, 1.f);

				switch (b.mode) {
				default: // Add/Lower
					h += b.strength * w;
					break;
				}
				h16[j * W + i] = vkutil::f32_to_f16(h);
			}
		}

		if (!isCoherent) {
			vmaFlushAllocation(engine.vmaAllocator, stage.allocation, 0, bytes);
		}
		vmaUnmapMemory(engine.vmaAllocator, stage.allocation);
	}

	// copy staging → image region
	{
		VkBufferImageCopy r{};
		r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, b.layer, 1 };
		r.imageOffset = { x0, y0, 0 };
		r.imageExtent = { W, K, 1 };
		vkCmdCopyBufferToImage(cmd, stage.buffer, heightmapImage.image, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
	}

	// barrier for sampling in terrain draw
	{
		VkImageMemoryBarrier2 ib{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		ib.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		ib.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		ib.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		ib.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		ib.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		ib.image = heightmapImage.image;
		ib.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, b.layer, 1 };

		VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &ib;
		vkCmdPipelineBarrier2(cmd, &dep);
	}
}



static inline void world_to_tile(const glm::vec2& worldXZ, float stepMeters, int& tx, int& tz) {
	tx = int(std::floor(worldXZ.x / stepMeters));
	tz = int(std::floor(worldXZ.y / stepMeters));
}

// Caller must provide: layer (array slice for this TileID), tileOrigin (world XZ of that tile),
// and worldScale (meters per texel for that tile).
void Renderer::editor_apply_cpu_brush(VkCommandBuffer cmd,
	const glm::vec2& worldXZ,
	uint32_t layer,
	const glm::vec2& tileOrigin,
	float worldScale,
	float radiusWorld,
	float strength,
	int   mode)
{
	BrushStroke b{};
	b.layer = layer;
	b.tileOrigin = tileOrigin;
	b.worldScale = worldScale;
	b.worldXZ = worldXZ;
	b.radiusWorld = radiusWorld;
	b.strength = strength;
	b.mode = mode;

	cpu_brush_small(cmd, b);
}


static inline bool is_host_coherent(VmaAllocator allocator, VmaAllocation alloc) {
	VkMemoryPropertyFlags props = 0;
	vmaGetAllocationMemoryProperties(allocator, alloc, &props);
	return (props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}

void Renderer::cpu_brush_stage_read(VkCommandBuffer cmd, const BrushStroke& b, BrushStageCtx& ctx)
{
	const uint32_t H = heightmapImage.imageExtent.width;
	if (!brush_rect_for_tile(b, H, ctx.x0, ctx.y0, ctx.x1, ctx.y1, ctx.cx, ctx.cy, ctx.rTex)) { ctx.bytes = 0; return; }

	ctx.W = uint32_t(ctx.x1 - ctx.x0 + 1);
	ctx.K = uint32_t(ctx.y1 - ctx.y0 + 1);
	if (ctx.W == 0 || ctx.K == 0) { ctx.bytes = 0; return; }

	ctx.bytes = VkDeviceSize(ctx.W) * VkDeviceSize(ctx.K) * sizeof(uint16_t);
	ctx.layer = b.layer;

	ctx.stage = engine.create_buffer(ctx.bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	engine.mainDeletionQueue.push_allocated_buffer(ctx.stage);

	VkMemoryPropertyFlags memProps = 0;
	vmaGetAllocationMemoryProperties(engine.vmaAllocator, ctx.stage.allocation, &memProps);
	ctx.isCoherent = (memProps & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

	// SHADER_READ_ONLY_OPTIMAL -> TRANSFER_SRC_OPTIMAL
	{
		VkImageMemoryBarrier2 ib{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		ib.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		ib.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		ib.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		ib.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		ib.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		ib.image = heightmapImage.image;
		ib.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, ctx.layer, 1 };

		VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &ib;
		vkCmdPipelineBarrier2(cmd, &dep);
	}

	// copy image → staging (from TRANSFER_SRC_OPTIMAL)
	{
		VkBufferImageCopy r{};
		r.bufferRowLength = 0;
		r.bufferImageHeight = 0;
		r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, ctx.layer, 1 };
		r.imageOffset = { ctx.x0, ctx.y0, 0 };
		r.imageExtent = { ctx.W, ctx.K, 1 };
		vkCmdCopyImageToBuffer(cmd, heightmapImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ctx.stage.buffer, 1, &r);
	}

	// TRANSFER_SRC_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL (restore for consistency)
	{
		VkImageMemoryBarrier2 ib{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		ib.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		ib.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		ib.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		ib.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		ib.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ib.image = heightmapImage.image;
		ib.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, ctx.layer, 1 };

		VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &ib;
		vkCmdPipelineBarrier2(cmd, &dep);
	}
}



void Renderer::cpu_brush_stage_cpu_modify(const BrushStroke& b, BrushStageCtx& ctx)
{
	if (ctx.bytes == 0) return;

	if (!ctx.isCoherent) {
		vmaInvalidateAllocation(engine.vmaAllocator, ctx.stage.allocation, 0, ctx.bytes);
	}

	void* p = nullptr;
	vmaMapMemory(engine.vmaAllocator, ctx.stage.allocation, &p);
	auto* h16 = reinterpret_cast<uint16_t*>(p);

	const float r2 = ctx.rTex * ctx.rTex;
	const float delta01 = b.strength / heightAmplitudeMeters; // meters -> [0..1]

	for (uint32_t j = 0; j < ctx.K; ++j) {
		const int ty = ctx.y0 + int(j);
		const float dy = (float(ty) + 0.5f) - ctx.cy;   // <-- center
		for (uint32_t i = 0; i < ctx.W; ++i) {
			const int tx = ctx.x0 + int(i);
			const float dx = (float(tx) + 0.5f) - ctx.cx; // <-- center
			const float d2 = dx * dx + dy * dy;
			if (d2 > r2) continue;

			float h01 = vkutil::f16_to_f32(h16[j * ctx.W + i]);
			float w = glm::clamp(1.f - (d2 / r2), 0.f, 1.f);

        switch (b.mode) { default: h01 += delta01 * w; break; }

								 h01 = glm::clamp(h01, 0.f, 1.f);
								 h16[j * ctx.W + i] = vkutil::f32_to_f16(h01);
		}
	}

	if (!ctx.isCoherent) {
		vmaFlushAllocation(engine.vmaAllocator, ctx.stage.allocation, 0, ctx.bytes);
	}
	vmaUnmapMemory(engine.vmaAllocator, ctx.stage.allocation);
}


void Renderer::cpu_brush_stage_write(VkCommandBuffer cmd, const BrushStageCtx& ctx)
{
	if (ctx.bytes == 0) return;

	// SHADER_READ_ONLY_OPTIMAL -> TRANSFER_DST_OPTIMAL
	{
		VkImageMemoryBarrier2 ib{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		ib.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		ib.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		ib.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		ib.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		ib.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		ib.image = heightmapImage.image;
		ib.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, ctx.layer, 1 };

		VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &ib;
		vkCmdPipelineBarrier2(cmd, &dep);
	}

	// copy staging → image (into TRANSFER_DST_OPTIMAL)
	{
		VkBufferImageCopy r{};
		r.bufferRowLength = 0;
		r.bufferImageHeight = 0;
		r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, ctx.layer, 1 };
		r.imageOffset = { ctx.x0, ctx.y0, 0 };
		r.imageExtent = { ctx.W, ctx.K, 1 };
		vkCmdCopyBufferToImage(cmd, ctx.stage.buffer, heightmapImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
	}

	// TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
	{
		VkImageMemoryBarrier2 ib{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		ib.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		ib.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		ib.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		ib.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		ib.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ib.image = heightmapImage.image;
		ib.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, ctx.layer, 1 };

		VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &ib;
		vkCmdPipelineBarrier2(cmd, &dep);
	}
}

