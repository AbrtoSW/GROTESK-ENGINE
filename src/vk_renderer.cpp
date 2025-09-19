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



Renderer::Renderer(VulkanEngine& engine) : engine(engine){}


Renderer::~Renderer() {
	init_renderer_cleanup();
}

void Renderer::init_renderer() {
	glslang::InitializeProcess();
	PipelineManager::init_PipelineCache();
	create_draw_image_renderpass();
	create_swapchain_renderpass();
	init_framebuffers();
	init_descriptors();
	init_pipelines();
	init_imgui();
	init_default_data();
}

void Renderer::init_pipelines() {
	init_backgound_pipelines();
	init_mesh_pipeline();
	metalRoughMaterial.build_pipelines(&engine, this);
}

void Renderer::render_frame() {


	VK_CHECK(vkWaitForFences(engine.device, 1, &engine.get_current_frame().renderFence, true, 1000000000));

	engine.get_current_frame().deletionQueue.flushFrameResources(engine.vmaAllocator);
	engine.get_current_frame().frameDescriptors->clear_pools(engine.device);

	VK_CHECK(vkResetFences(engine.device, 1, &engine.get_current_frame().renderFence));

	uint32_t swapchainImageIndex = 0;


	VkResult result = vkAcquireNextImageKHR(engine.device, engine.swapchain, 1000000000, engine.get_current_frame().swapchainSemaphore, nullptr, &swapchainImageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		engine.resize_requested = true;
		return;
	}

	

	VkSemaphore currentRenderSemaphore = engine.swapchainImageRenderSemaphores[swapchainImageIndex];

	drawExtent.width = std::min(engine.swapchainExtent.width, engine.drawImage.imageExtent.width);
	drawExtent.height = std::min(engine.swapchainExtent.height, engine.drawImage.imageExtent.height);


	VkCommandBuffer cmd = engine.get_current_frame().mainCommandBuffer;

	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	vkutil::transition_image(cmd, engine.drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	render_background(cmd);

	vkutil::transition_image(cmd, engine.drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);



	init_draw_image_renderpass(cmd);

	vkutil::transition_image(cmd, engine.swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vkutil::copy_image_to_image(cmd, engine.drawImage.image, engine.swapchainImages[swapchainImageIndex], drawExtent, engine.swapchainExtent);
	vkutil::transition_image(cmd, engine.swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);


	init_swapchain_renderpass(cmd, swapchainImageIndex);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);

	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, engine.get_current_frame().swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, currentRenderSemaphore);

	VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(engine.graphicsQueue, 1, &submit, engine.get_current_frame().renderFence));

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

	PipelineManager::destroyPipelineCache();
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

void Renderer::init_draw_image_renderpass(VkCommandBuffer cmd) {
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

	render_pass_geometry(cmd);

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
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
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
		singleImageDescriptorLayout = builder.build(engine.device, VK_SHADER_STAGE_FRAGMENT_BIT);
	}


	globalDescriptorAllocator.defer_pool_main_deletion();
	engine.mainDeletionQueue.push_descriptor_set_layout(drawImageDescriptorLayout);
	engine.mainDeletionQueue.push_descriptor_set_layout(gpuSceneDataDescriptorLayout);
	engine.mainDeletionQueue.push_descriptor_set_layout(singleImageDescriptorLayout);

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
		};

		engine.frames[i].frameDescriptors = std::make_unique<DescriptorAllocatorGrowable>();
		engine.frames[i].frameDescriptors->init(engine.device, 1000, frame_sizes);

		engine.frames[i].frameDescriptors->defer_pool_main_deletion();
	}

}

void Renderer::init_backgound_pipelines() {

	// Pipelines

	VkPipelineLayout gradientPipelineLayout;

	VkPipelineLayoutCreateInfo computeLayout{};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;
	computeLayout.pSetLayouts = &drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstant{};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(engine.device, &computeLayout, nullptr, &gradientPipelineLayout));

	VkShaderModule gradientComputeShader;
	if (!shaderUtil::load_shader_module("C:/Users/Alberto/source/repos/GROTESK/GROTESK/res/shaders/gradient_color.comp.spv", engine.device, &gradientComputeShader)) {
		fmt::print("compute shader did not load \n");
	}

	VkShaderModule skyComputeShader;
	if (!shaderUtil::load_shader_module("C:/Users/Alberto/source/repos/GROTESK/GROTESK/res/shaders/sky.comp.spv", engine.device, &skyComputeShader)) {
		fmt::print("compute shader did not load \n");
	}

	VkPipelineShaderStageCreateInfo stageinfo{};
	stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageinfo.pNext = nullptr;
	stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageinfo.module = gradientComputeShader;
	stageinfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = gradientPipelineLayout;
	computePipelineCreateInfo.stage = stageinfo;

	ComputeEffect gradient;
	gradient.layout = gradientPipelineLayout;
	gradient.name = "gradient";
	gradient.data = {};
	gradient.data.data1 = glm::vec4(1, 0, 0, 1);
	gradient.data.data2 = glm::vec4(0, 0, 1, 1);

	VK_CHECK(vkCreateComputePipelines(engine.device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, engine.vkAllocator, &gradient.pipeline));

	computePipelineCreateInfo.stage.module = skyComputeShader;

	ComputeEffect sky;
	sky.layout = gradientPipelineLayout;
	sky.name = "sky";
	sky.data = {};
	sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

	VK_CHECK(vkCreateComputePipelines(engine.device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

	backgroundEffects.push_back(gradient);
	backgroundEffects.push_back(sky);

	vkDestroyShaderModule(engine.device, gradientComputeShader, nullptr);
	vkDestroyShaderModule(engine.device, skyComputeShader, nullptr);


	gradientPipelineID = managePipeline.createPipelineID();
	skyPipelineID = managePipeline.createPipelineID();
	gradientPipelineLayoutID = managePipeline.createLayoutID();
	managePipeline.store_pipeline(gradientPipelineID, gradientPipelineLayoutID,gradient.pipeline, gradientPipelineLayout);
	managePipeline.store_pipeline(skyPipelineID, gradientPipelineLayoutID,sky.pipeline,gradientPipelineLayout);

}


void Renderer::init_mesh_pipeline() {

	meshPipeline.type = PipelineType::Graphics;
	meshPipeline.shader.vertexShader.file = "C:/Users/Alberto/source/repos/GROTESK/GROTESK/res/shaders/colored_triangle_mesh_test.vert";
	meshPipeline.shader.fragmentShader.file = "C:/Users/Alberto/source/repos/GROTESK/GROTESK/res/shaders/tex_image_test.frag";

	meshPipeline.shader.vertexShader.lastModified = shaderUtil::getFileTimeStamp(meshPipeline.shader.vertexShader.file);
	meshPipeline.shader.fragmentShader.lastModified = shaderUtil::getFileTimeStamp(meshPipeline.shader.fragmentShader.file);

	meshPipeline.shader.vertexShader.stage = VK_SHADER_STAGE_VERTEX_BIT;
	meshPipeline.shader.fragmentShader.stage = VK_SHADER_STAGE_FRAGMENT_BIT;


	VkShaderModule vertexShader = shaderUtil::compileToSPV(engine.device, meshPipeline.shader.vertexShader.file, EShLangVertex);
	VkShaderModule fragmentShader = shaderUtil::compileToSPV(engine.device, meshPipeline.shader.fragmentShader.file, EShLangFragment);


	auto* meshPipelineConfig = meshPipeline.getGraphicsConfig();

	meshPipelineConfig->pushConstantRange.offset = 0;
	meshPipelineConfig->pushConstantRange.size = sizeof(GPUDrawPushConstants);
	meshPipelineConfig->pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	meshPipelineConfig->layoutInfo = vkinit::pipeline_layout_create_info();
	meshPipelineConfig->layoutInfo.pPushConstantRanges = &meshPipelineConfig->pushConstantRange;
	meshPipelineConfig->layoutInfo.pushConstantRangeCount = 1;
	meshPipelineConfig->layoutInfo.pSetLayouts = &singleImageDescriptorLayout;
	meshPipelineConfig->layoutInfo.setLayoutCount = 1;


	VK_CHECK(vkCreatePipelineLayout(engine.device, &meshPipelineConfig->layoutInfo, nullptr, &meshPipeline.pipelineLayout.layout));

	PipelineBuilder pipelineBuilder;
	//use the triangle layout we created
	pipelineBuilder.res->pipelineLayout = meshPipeline.pipelineLayout;
	//connecting the vertex and pixel shaders to the pipeline
	pipelineBuilder.set_shaders(vertexShader, fragmentShader);
	//it will draw triangles
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	//filled triangles
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	//no backface culling
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	//no multisampling
	pipelineBuilder.set_multisampling_none();
	//no blending
	//pipelineBuilder.enable_blending_additive();
	pipelineBuilder.disable_blending();
	pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
	pipelineBuilder.set_renderpass(drawImageRenderPass);

	
	//connect the image format we will draw into, from draw image
	//pipelineBuilder.set_color_attachment_format(engine.drawImage.imageFormat);
	//pipelineBuilder.set_depth_format(engine.depthImage.imageFormat);

	//finally build the pipeline
	meshPipeline.pipeline = pipelineBuilder.build_pipeline(engine.device, RenderMode::Classic, &meshPipeline);

	fmt::print("Registered vertex shader: {} lastModified: {} after meshPipeline.pipline build function\n",
		meshPipeline.shader.vertexShader.file,
		meshPipeline.shader.vertexShader.lastModified.time_since_epoch().count());


	//clean structures
	vkDestroyShaderModule(engine.device, vertexShader, nullptr);
	vkDestroyShaderModule(engine.device, fragmentShader, nullptr);
	
	managePipeline.manage_pipeline(meshPipeline, TrackShader::Yes);
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

	std::array<Vertex, 4> rect_vertices;

	rect_vertices[0].position = { 0.5,-0.5, 0 };
	rect_vertices[1].position = { 0.5,0.5, 0 };
	rect_vertices[2].position = { -0.5,-0.5, 0 };
	rect_vertices[3].position = { -0.5,0.5, 0 };

	rect_vertices[0].color = { 0,0, 0,1 };
	rect_vertices[1].color = { 0.5,0.5,0.5 ,1 };
	rect_vertices[2].color = { 1,0, 0,1 };
	rect_vertices[3].color = { 0,1, 0,1 };

	std::array<uint32_t, 6> rect_indices;

	rect_indices[0] = 0;
	rect_indices[1] = 1;
	rect_indices[2] = 2;

	rect_indices[3] = 2;
	rect_indices[4] = 1;
	rect_indices[5] = 3;

	rectangle = engine.uploadMesh(rect_indices, rect_vertices);

	testMeshes = loadGltfMeshes(&engine, "C:/Users/Alberto/source/repos/GROTESK/GROTESK/res/assets/basicmesh.glb").value();

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


	GLTFMetallic_Roughness::MaterialResources materialResources;
	materialResources.colorImage = whiteImage;
	materialResources.colorSampler = defaultSamplerLinear;
	materialResources.metalRoughImage = whiteImage;
	materialResources.metalRoughSampler = defaultSamplerLinear;

	AllocatedBuffer materialConstants = engine.create_buffer(sizeof(GLTFMetallic_Roughness::MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	void* data;

	vmaMapMemory(engine.vmaAllocator, materialConstants.allocation, &data);

	GLTFMetallic_Roughness::MaterialConstants* sceneUniformData = reinterpret_cast<GLTFMetallic_Roughness::MaterialConstants*>(data);

	sceneUniformData->colorFactors = glm::vec4{ 1,1,1,1 };
	sceneUniformData->metal_rough_factors = glm::vec4{ 1,0.5,0,0 };

	engine.mainDeletionQueue.push_allocated_buffer(materialConstants);

	materialResources.dataBuffer = materialConstants.buffer;
	materialResources.dataBufferOffset = 0;

	defaultData = metalRoughMaterial.write_material(engine.device, MaterialPass::MainColor, materialResources, globalDescriptorAllocator);


}

void Renderer::render_pass_geometry(VkCommandBuffer cmd) {


	//set dynamic viewport and scissor
	VkViewport viewport = {};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = drawExtent.width;
	viewport.height = drawExtent.height;
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = viewport.width;
	scissor.extent.height = viewport.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, managePipeline.get_pipeline(meshPipeline.pipelineID));

	VkDescriptorSet imageSet = engine.get_current_frame().frameDescriptors->allocate(engine.device, singleImageDescriptorLayout);

	{
		DescriptorWriter writer;
		writer.write_image(0, errorCheckerBoardImage.imageView, defaultSamplerNearest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

		writer.update_set(engine.device, imageSet);
	}
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, managePipeline.get_layout(meshPipeline.pipelineLayout.pipelineLayoutID), 0, 1, &imageSet, 0, nullptr);

	GPUDrawPushConstants pushConstants;

	glm::mat4 view = glm::translate(glm::vec3{ 0,0,-5 });
	// camera projection
	glm::mat4 projection = glm::perspective(glm::radians(70.f), (float)drawExtent.width / (float)drawExtent.height, 0.1f, 10000.0f);

	projection[1][1] *= -1;

	//pushConstants.worldMatrix = glm::mat4(1.0f);	
	pushConstants.worldMatrix = projection * view;
	pushConstants.vertexBuffer = testMeshes[2]->meshBuffers.vertexBufferAddress;

	vkCmdPushConstants(cmd, managePipeline.get_layout(meshPipeline.pipelineLayout.pipelineLayoutID), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
	vkCmdBindIndexBuffer(cmd, testMeshes[2]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd, testMeshes[2]->surfaces[0].count, 1, testMeshes[2]->surfaces[0].startIndex, 0, 0);

	AllocatedBuffer gpuSceneDataBuffer = engine.create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	engine.get_current_frame().deletionQueue.push_allocated_buffer(gpuSceneDataBuffer);

	void* mappedData = nullptr;
	vmaMapMemory(engine.vmaAllocator, gpuSceneDataBuffer.allocation, &mappedData);
	*(GPUSceneData*)mappedData = sceneData;
	vmaUnmapMemory(engine.vmaAllocator, gpuSceneDataBuffer.allocation);

	VkDescriptorSet globalDescriptor = engine.get_current_frame().frameDescriptors->allocate(engine.device, gpuSceneDataDescriptorLayout);
	
	DescriptorWriter writer;
	writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.update_set(engine.device, globalDescriptor);


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

void Renderer::render_background(VkCommandBuffer cmd) {

	ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];

	//clear image
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

	// bind the descriptor set containing the draw image for the compute pipeline
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, managePipeline.get_layout(gradientPipelineLayoutID), 0, 1, &drawImageDescriptors, 0, nullptr);


	vkCmdPushConstants(cmd, managePipeline.get_layout(gradientPipelineLayoutID), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);
	// execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
	vkCmdDispatch(cmd, std::ceil(drawExtent.width / 16.0), std::ceil(drawExtent.height / 16.0), 1);
}

void Renderer::create_draw_image_renderpass() {
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = engine.drawImage.imageFormat; // VK_FORMAT_R16G16B16A16_SFLOAT
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // we load because we get an image from background pipelines, do clear if we dont render from background anymore
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

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

	VmaAllocationCreateInfo allocinfo = {};
	allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VK_CHECK(vmaCreateImage(engine.vmaAllocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT) {
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	VkImageViewCreateInfo view_info = vkinit::imageview_create_info(format, newImage.image, aspectFlag);
	view_info.subresourceRange.levelCount = img_info.mipLevels;

	VK_CHECK(vkCreateImageView(engine.device, &view_info, nullptr, &newImage.imageView));

	return newImage;

}

AllocatedImage Renderer::create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
	size_t data_size = size.depth * size.width * size.height * 4;
	AllocatedBuffer uploadbuffer = engine.create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	memcpy(uploadbuffer.info.pMappedData, data, data_size);

	AllocatedImage new_image = create_image(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

	engine.immediateCommandSubmit([&](VkCommandBuffer cmd) {
		vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = size;

		vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
			&copyRegion);

		vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});

	vmaDestroyBuffer(engine.vmaAllocator, uploadbuffer.buffer, uploadbuffer.allocation);

	return new_image;
}


void PipelineManager::init_PipelineCache() {
	VkPipelineCacheCreateInfo cacheInfo{};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	cacheInfo.pNext = nullptr;
	cacheInfo.flags = 0;              
	cacheInfo.initialDataSize = 0;    
	cacheInfo.pInitialData = nullptr; 


	VK_CHECK(vkCreatePipelineCache(VulkanEngine::Get().device, &cacheInfo, nullptr, &pipelineCache));
}

void PipelineManager::manage_pipeline(PipelineResource& res, TrackShader trackShader, PipelineLayoutResource* layoutShared) {

	fmt::print("managing pipeline called \n");


	auto pipelineFinder = pipelineLookup.find(res.pipelineID);

	if (pipelineFinder == pipelineLookup.end()) {
		res.pipelineID = PipelineManager::createPipelineID();
		pipelineLookup[res.pipelineID] = res.pipeline;

		fmt::print("Created pipeline ID {}, for Pipeline{}\n", res.pipelineID, (void*)res.pipeline);

		VulkanEngine::Get().mainDeletionQueue.push_pipeline(res.pipeline);

	}
	else if (pipelineFinder != pipelineLookup.end() && pipelineFinder->second) {

		fmt::print("init pipeline resource was called for hot reloading pipeline \n");

		VkPipeline oldPipeline = pipelineFinder->second;

		auto& queue = VulkanEngine::Get().mainDeletionQueue.pipelines;

		fmt::print("This old pipeline is being removed: {}\n", (void*)oldPipeline);
		queue.erase(std::remove(queue.begin(), queue.end(), oldPipeline), queue.end());

		pipelineFinder->second = res.pipeline;

		queue.push_back(res.pipeline);
	}


	auto layoutFinder = layoutLookup.find(res.pipelineLayout.pipelineLayoutID);

	if (layoutFinder == layoutLookup.end()) {

		if (res.pipelineLayout.isOwned == LayoutOwnership::Uninitialized) {
			fmt::print("initialize the pipeline layout");
			return;
		}

		if (res.pipelineLayout.isOwned == LayoutOwnership::True) {
			res.pipelineLayout.pipelineLayoutID = PipelineManager::createLayoutID();
			layoutLookup[res.pipelineLayout.pipelineLayoutID] = res.pipelineLayout.layout;
			fmt::print("Created owned pipeline layout ID {}, for Pipeline layout {}\n", (void*)res.pipelineLayout.layout, (void*)res.pipelineLayout.layout);

			VulkanEngine::Get().mainDeletionQueue.push_pipeline_layout(res.pipelineLayout.layout);
		}
	}

	if (layoutShared) {
		auto sharedFinder = layoutLookup.find(layoutShared->pipelineLayoutID);

		if (sharedFinder == layoutLookup.end() && layoutShared->layout != VK_NULL_HANDLE && layoutShared->isShared == SharedLayout::Yes) {
				layoutShared->pipelineLayoutID = PipelineManager::createLayoutID();

				layoutLookup[layoutShared->pipelineLayoutID] = layoutShared->layout;

				sharedLayouts[layoutShared->pipelineLayoutID] = {};
				fmt::print("Created shared layout ID {}, handle {}\n", (void*)layoutShared->pipelineLayoutID, (void*)layoutShared->layout);

				sharedLayouts[layoutShared->pipelineLayoutID].push_back(res.pipeline);
				fmt::print("Added pipeline {} to shared layout ID {}, handle {}\n", (void*)res.pipeline, (void*)layoutShared->pipelineLayoutID, (void*)layoutShared->layout);
				VulkanEngine::Get().mainDeletionQueue.push_pipeline_layout(layoutShared->layout);
		}
		else if (layoutShared->isShared == SharedLayout::Yes && layoutShared->layout != VK_NULL_HANDLE) {
			sharedLayouts[layoutShared->pipelineLayoutID].push_back(res.pipeline);
			fmt::print("Added pipeline {} to existing shared layout ID {}, handle {}\n", (void*)res.pipeline, (void*)layoutShared->pipelineLayoutID, (void*)layoutShared->layout);
		}
	}

	if (trackShader == TrackShader::Yes) {
		track_shaders_for_hotload(res);
	}
}

void PipelineManager::store_pipeline(PipelineID pID, LayoutID lId, VkPipeline pipeline, VkPipelineLayout layout) {


	auto plook = pipelineLookup.find(pID);
	if (plook != pipelineLookup.end()) {
		plook->second = pipeline;
	}
	else {
		VulkanEngine::Get().mainDeletionQueue.push_pipeline(pipeline);

		pipelineLookup[pID] = pipeline;
	}


	auto it = layoutLookup.find(lId);
	if (it == layoutLookup.end()) {
		VulkanEngine::Get().mainDeletionQueue.push_pipeline_layout(layout);
		layoutLookup[lId] = layout;
	} 

}

void PipelineManager::track_shaders_for_hotload(PipelineResource& resource) {

	fmt::print("Before link_shader: vertex='{}', fragment='{}'\n",
		resource.shader.vertexShader.file,
		resource.shader.fragmentShader.file);

	if (!resource.shader.vertexShader.file.empty()) {
		shaderMap[resource.shader.vertexShader.file].push_back(&resource);
		fmt::print("Registered vertex shader: {} lastModified: {}\n",
			resource.shader.vertexShader.file,
			resource.shader.vertexShader.lastModified.time_since_epoch().count());
	}

	if (!resource.shader.fragmentShader.file.empty()) {

		shaderMap[resource.shader.fragmentShader.file].push_back(&resource);
		fmt::print("Registered fragment shader: {} lastModified: {}\n",
			resource.shader.fragmentShader.file,
			resource.shader.fragmentShader.lastModified.time_since_epoch().count());
	}

	if (!resource.shader.geometryShader.file.empty()) {
		shaderMap[resource.shader.geometryShader.file].push_back(&resource);
		fmt::print("Registered geometry shader: {}\n", resource.shader.geometryShader.file);
	}

	if (!resource.shader.computeShader.file.empty()) {
		shaderMap[resource.shader.computeShader.file].push_back(&resource);
		fmt::print("Registered compute shader: {}\n", resource.shader.computeShader.file);
	}
}

void Renderer::HotloadShader() {
	fmt::print("hotloadShader called\n");
	auto& shaderMap = PipelineManager::get_shaderMap();

	std::set<PipelineResource*>  pipelinesToRebuild;


	for (auto& [file, resource] : shaderMap) {
		std::filesystem::file_time_type currentWriteTimeStamp = shaderUtil::getFileTimeStamp(file);
		for (auto* r : resource) {
		
			if (file == r->shader.vertexShader.file) {
				fmt::print("Checking file: {} old: {} new: {}\n",
					file,
					r->shader.vertexShader.lastModified.time_since_epoch().count(),
					currentWriteTimeStamp.time_since_epoch().count());

				if (currentWriteTimeStamp != r->shader.vertexShader.lastModified) {
					r->shader.vertexShader.lastModified = currentWriteTimeStamp;
					pipelinesToRebuild.insert(r);
				}
			}
			if (file == r->shader.fragmentShader.file) {

				fmt::print("Checking file: {} old: {} new: {}\n",
					file,
					r->shader.fragmentShader.lastModified.time_since_epoch().count(),
					currentWriteTimeStamp.time_since_epoch().count());


				if (currentWriteTimeStamp != r->shader.fragmentShader.lastModified) {
					r->shader.fragmentShader.lastModified = currentWriteTimeStamp;
					pipelinesToRebuild.insert(r);
				}
			}
			if (file == r->shader.geometryShader.file) {
				fmt::print("Checking file: {} old: {} new: {}\n",
					file,
					r->shader.geometryShader.lastModified.time_since_epoch().count(),
					currentWriteTimeStamp.time_since_epoch().count());
				if (currentWriteTimeStamp != r->shader.geometryShader.lastModified) {
					r->shader.geometryShader.lastModified = currentWriteTimeStamp;
					pipelinesToRebuild.insert(r);
				}
			}
			if (file == r->shader.computeShader.file) {
				fmt::print("Checking file: {} old: {} new: {}\n",
					file,
					r->shader.computeShader.lastModified.time_since_epoch().count(),
					currentWriteTimeStamp.time_since_epoch().count());
				if (currentWriteTimeStamp != r->shader.computeShader.lastModified) {
					r->shader.computeShader.lastModified = currentWriteTimeStamp;
					pipelinesToRebuild.insert(r);
				}
			}
		}
	}

	for (auto* r : pipelinesToRebuild) {

		rebuild(engine.device, *r);
		
		managePipeline.manage_pipeline(*r, TrackShader::Yes);
	}
	pipelinesToRebuild.clear();

}

VkPipelineLayout PipelineManager::get_layout(LayoutID id) const {
	auto it = layoutLookup.find(id);
	if (it != layoutLookup.end()) return it->second;
	return VK_NULL_HANDLE;
}

VkPipeline PipelineManager::get_pipeline(PipelineID id) const {
	auto it = pipelineLookup.find(id);
	if (it != pipelineLookup.end()) return it->second;
	return VK_NULL_HANDLE;
}

void PipelineManager::destroyPipelineCache() {
	if (pipelineCache != VK_NULL_HANDLE) {
		vkDestroyPipelineCache(VulkanEngine::Get().device, pipelineCache, nullptr);
		pipelineCache = VK_NULL_HANDLE;
	}
}



VkPipeline Renderer::rebuild(VkDevice device, PipelineResource& res)  {
	fmt::print("rebuildPipelines called\n");

	VkPipeline oldPipeline = res.pipeline;

	auto* resConfig = res.getGraphicsConfig();


	fmt::print("old pipeline object identification is {}\n ", (void*)oldPipeline);
	fmt::print("RenderPass handle on rebuild: {}\n", (void*)resConfig->renderPass);

	resConfig->shaderStages.clear();

	VkShaderModule vertexModule = VK_NULL_HANDLE;
	VkShaderModule fragmentModule = VK_NULL_HANDLE;

	if (!res.shader.vertexShader.file.empty()) {
		vertexModule = shaderUtil::compileToSPV(device, res.shader.vertexShader.file, EShLangVertex);
		resConfig->shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(
			VK_SHADER_STAGE_VERTEX_BIT, vertexModule));
	}

	if (!res.shader.fragmentShader.file.empty()) {
		fragmentModule = shaderUtil::compileToSPV(device, res.shader.fragmentShader.file, EShLangFragment);
		resConfig->shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(
			VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule));
	}



	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.pNext = &resConfig->renderInfo;

	pipelineInfo.stageCount = (uint32_t)resConfig->shaderStages.size();
	pipelineInfo.pStages = resConfig->shaderStages.data();
	pipelineInfo.pVertexInputState = &resConfig->vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &resConfig->inputAssembly;
	pipelineInfo.pViewportState = &resConfig->viewportStateInfo;
	pipelineInfo.pRasterizationState = &resConfig->rasterizer;
	pipelineInfo.pMultisampleState = &resConfig->multisampling;
	pipelineInfo.pColorBlendState = &resConfig->colorBlendingInfo;
	pipelineInfo.pDepthStencilState = &resConfig->depthStencil;
	pipelineInfo.layout = res.pipelineLayout.layout;
	pipelineInfo.pDynamicState = &resConfig->dynamicStateInfo;

	if (resConfig->renderMode == RenderMode::Dynamic) {
		pipelineInfo.renderPass = VK_NULL_HANDLE;
		pipelineInfo.subpass = 0;
		resConfig->renderPass = VK_NULL_HANDLE;
	}
	else {
		pipelineInfo.renderPass = resConfig->renderPass;
		pipelineInfo.subpass = 0;
	}

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

	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(device, PipelineManager::pipelineCache, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
		fmt::println("Failed to rebuild pipeline");
		return VK_NULL_HANDLE;
	}

	// Replace the old pipeline
	res.pipeline = newPipeline;
	if (oldPipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(device, oldPipeline, nullptr);
	}

	if (vertexModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertexModule, nullptr);
	if (fragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragmentModule, nullptr);

	return newPipeline;
}


void GLTFMetallic_Roughness::build_pipelines(VulkanEngine* engine, Renderer* renderer) {

	opaquePipeline.type = PipelineType::Graphics;
	transparentPipeline.type = PipelineType::Graphics;
	
	opaquePipeline.pipelineLayout.isOwned = LayoutOwnership::False;
	transparentPipeline.pipelineLayout.isOwned = LayoutOwnership::False;

	opaquePipeline.shader.vertexShader.file = "C:/Users/Alberto/source/repos/GROTESK/GROTESK/res/shaders/mesh.vert";
	opaquePipeline.shader.fragmentShader.file = "C:/Users/Alberto/source/repos/GROTESK/GROTESK/res/shaders/mesh.frag";

	opaquePipeline.shader.vertexShader.lastModified = shaderUtil::getFileTimeStamp(opaquePipeline.shader.vertexShader.file);
	opaquePipeline.shader.fragmentShader.lastModified = shaderUtil::getFileTimeStamp(opaquePipeline.shader.fragmentShader.file);


	VkShaderModule meshVertShader = shaderUtil::compileToSPV(engine->device, opaquePipeline.shader.vertexShader.file, EShLangVertex);
	VkShaderModule meshFragShader = shaderUtil::compileToSPV(engine->device, opaquePipeline.shader.fragmentShader.file, EShLangFragment);


	auto* config = opaquePipeline.getGraphicsConfig();

	config->pushConstantRange.offset = 0;
	config->pushConstantRange.size = sizeof(GPUDrawPushConstants);
	config->pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	DescriptorLayoutBuilder layoutBuilder;
	layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	layoutBuilder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	materialLayout = layoutBuilder.build(engine->device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

	VkDescriptorSetLayout layouts[] = { renderer->gpuSceneDataDescriptorLayout, materialLayout };

	config->layoutInfo = vkinit::pipeline_layout_create_info();
	config->layoutInfo.setLayoutCount = 2;
	config->layoutInfo.pSetLayouts = layouts;
	config->layoutInfo.pPushConstantRanges = &config->pushConstantRange;
	config->layoutInfo.pushConstantRangeCount = 1;


	//i should figure out a way to make shared pipelines as a map for pipelines that may or may not used the same descriptor layout 
	PipelineLayoutResource sharedLayout;
	sharedLayout.isShared = SharedLayout::Yes;

	VK_CHECK(vkCreatePipelineLayout(engine->device, &config->layoutInfo, engine->vkAllocator, &sharedLayout.layout));

	PipelineBuilder builder;
	builder.set_shaders(meshVertShader, meshFragShader);
	builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	builder.set_multisampling_none();
	builder.disable_blending();
	builder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
	builder.set_renderpass(renderer->drawImageRenderPass);
	builder.res->pipelineLayout.layout = sharedLayout.layout;

	opaquePipeline.pipeline = builder.build_pipeline(engine->device, RenderMode::Classic, &opaquePipeline);

	builder.enable_blending_additive();
	builder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
	builder.res->pipelineLayout.layout = sharedLayout.layout;
	transparentPipeline.pipeline = builder.build_pipeline(engine->device, RenderMode::Classic, &transparentPipeline);

	vkDestroyShaderModule(engine->device, meshVertShader, nullptr);
	vkDestroyShaderModule(engine->device, meshFragShader, nullptr);

	renderer->managePipeline.manage_pipeline(opaquePipeline, TrackShader::Yes, &sharedLayout);
	renderer->managePipeline.manage_pipeline(transparentPipeline, TrackShader::Yes, &sharedLayout);

	engine->mainDeletionQueue.push_descriptor_set_layout(materialLayout);
}

MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator) {

	MaterialInstance matData;
	matData.passType = pass;

	if (pass == MaterialPass::Transparent) {
		matData.pipeline = &transparentPipeline;
	}
	else {
		matData.pipeline = &opaquePipeline;
	}
	matData.materialSet = descriptorAllocator.allocate(device, materialLayout);

	writer.clear();

	writer.write_buffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.write_image(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	writer.write_image(2, resources.metalRoughImage.imageView, resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	writer.update_set(device, matData.materialSet);

	return matData;
}

