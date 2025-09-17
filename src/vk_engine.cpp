//defines
#define VMA_IMPLEMENTATION
//> includes
#include "vk_engine.h"
#include "vk_images.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include "vk_pipelines.h"
#include "vk_renderer.h"
//third party
#include "vma/vk_mem_alloc.h"
#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_sdl3.h"
#include "vkbootstrap/VkBootstrap.h"
//system includes
#include <chrono>
#include <thread>

VulkanEngine* VulkanEngine::loadedEngine = nullptr;

VulkanEngine& VulkanEngine::Get() {
	if (!loadedEngine) {
		throw std::runtime_error("VulkanEngine not initialized. Call init() first.");
	}
	return *loadedEngine;
}
constexpr bool useValidationLayers = true;


void VulkanEngine::init() {
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

	init_SDL3();
	init_vulkan();
	init_swapchain_resources();
	init_commands();
	init_sync_structures();
	renderer = new Renderer(*this);
	renderer->init_renderer();

    isInitialized = true;
}

void VulkanEngine::run() {

	SDL_Event event;
	bool quit = false;
	fmt::print("GROTESK RUNNING\n");
	// main loop
	while (!quit) {
		// Handle events on queue
		while (SDL_PollEvent(&event) != 0) {
			ImGui_ImplSDL3_ProcessEvent(&event);
			// close the window when user alt-f4s or clicks the X button
			switch (event.type) {
			case SDL_EVENT_QUIT:
				quit = true;
				break;
			case SDL_EVENT_WINDOW_MINIMIZED:
				stop_rendering = true;
				break;
			case SDL_EVENT_WINDOW_RESTORED:
				stop_rendering = false;
				break;
				// KEYBOARD EVENTS WHEN PRESSED
			case SDL_EVENT_KEY_DOWN:
				switch (event.key.key) {
					case SDLK_ESCAPE:
						quit = true;
						break;

					case SDLK_H:
						hotload_requested = true;
						break;
				}
				
				
			default:
				break;
			}
		}

		// do not draw if we are minimized
		if (stop_rendering) {
			// throttle the speed to avoid the endless spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		if (ImGui::Begin("background")) {
			ComputeEffect& selected = renderer->backgroundEffects[renderer->currentBackgroundEffect];
			ImGui::Text("Selected effect: ", selected.name);

			ImGui::SliderInt("Effect Index", &renderer->currentBackgroundEffect, 0, renderer->backgroundEffects.size() - 1);

			ImGui::InputFloat4("data1", (float*)&selected.data.data1);
			ImGui::InputFloat4("data2", (float*)&selected.data.data2);
			ImGui::InputFloat4("data3", (float*)&selected.data.data3);
			ImGui::InputFloat4("data4", (float*)&selected.data.data4);
		}
		ImGui::End();

		ImGui::Render();


		//might make this implementation for the windowed view option
		// when resizing there is slow down upon deletion, figure out the reason and see if optimize possible
		//add borderless fullscreen, and fullscreen, make sure the new pools and sets are allocated correctly
		if (resize_requested == true) {

			vkDeviceWaitIdle(device);
			mainDeletionQueue.resizeFlush(device, vmaAllocator);

			destroy_swapchain();

			int w, h;
			SDL_GetWindowSize(window, &w, &h);
			if (w == 0 || h == 0) return;
			windowExtent.width = w;
			windowExtent.height = h;

			init_swapchain_resources();

			renderer->init_framebuffers();

			renderer->init_descriptors();

			resize_requested = false;
		}

		if (hotload_requested == true) {
			fmt::print("hotload initiated: {}\n", hotload_requested);

			vkDeviceWaitIdle(device);

			renderer->HotloadShader();

			hotload_requested = false;
			fmt::print("hotload finished: {}\n", hotload_requested);

		}


		renderer->render_frame();
	}
}

bool VulkanEngine::init_SDL3() {

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		printf("Error: SDL_Init(): %s\n", SDL_GetError());
		return false;
	}

	 main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());


	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	window = SDL_CreateWindow("GROTESK", (int)(windowExtent.width * main_scale), (int)(windowExtent.height * main_scale), window_flags);
	if (!window) {
		printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
		SDL_Quit();
		return false;
	}
	

	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	SDL_ShowWindow(window);

	return true;
}

void VulkanEngine::init_vulkan() {

	{
		uint32_t sdl_extensions_count = 0;
		const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
		for (uint32_t n = 0; n < sdl_extensions_count; n++)
			extensions.push_back(sdl_extensions[n]);
	}

	vkb::InstanceBuilder builder;

	auto inst_ret = builder.set_app_name("GROTESK")
		.request_validation_layers(useValidationLayers)
		.use_default_debug_messenger()
		.require_api_version(1, 3, 0)
		.enable_extensions(extensions)
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	instance = vkb_inst.instance;
	debug_messenger = vkb_inst.debug_messenger;

	if (SDL_Vulkan_CreateSurface(window, instance, vkAllocator, &surface) == 0)
	{
		printf("Failed to create Vulkan surface.\n");
		return;
	}


	VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features.dynamicRendering = true;
	features.synchronization2 = true;

	VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing = true;

	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice chosenPhysicalDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features)
		.set_required_features_12(features12)
		.set_surface(surface)
		.select()
		.value();

	vkb::DeviceBuilder deviceBuilder{ chosenPhysicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build().value();

	device = vkbDevice.device;
	physicalDevice = chosenPhysicalDevice.physical_device;

	graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = physicalDevice;
	allocatorInfo.device = device;
	allocatorInfo.instance = instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &vmaAllocator);
	
	
}

void VulkanEngine::cleanup() {

	if (!isInitialized) {
		return;
	}

		//make sure the gpu has stopped doing its things
	vkDeviceWaitIdle(device);


	for (int i = 0; i < FRAME_OVERLAP; i++) {

		vkDestroyCommandPool(device, frames[i].commandPool, vkAllocator);

		//destroy sync objects
		vkDestroyFence(device, frames[i].renderFence, vkAllocator);

		vkDestroySemaphore(device, frames[i].swapchainSemaphore, vkAllocator);
			


		frames[i].deletionQueue.flushFrameResources(vmaAllocator);
		
		frames[i].frameDescriptors.reset();
	}

	mainDeletionQueue.flushMainResources(device,vmaAllocator);

	delete renderer;

	destroy_swapchain();
    
	vkDestroySurfaceKHR(instance, surface, vkAllocator);
	vkDestroyDevice(device, vkAllocator);
	vkb::destroy_debug_utils_messenger(instance, debug_messenger);
	vkDestroyInstance(instance, vkAllocator);

	SDL_DestroyWindow(window);
}

void VulkanEngine::init_swapchain_resources() {
	create_swapchain(windowExtent.width, windowExtent.height);
	create_offscreen_resources();
}

void VulkanEngine::init_commands() {

	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++) {

		VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frames[i].commandPool));

		// allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(frames[i].commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &frames[i].mainCommandBuffer));

	}
	

	VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, vkAllocator, &immediateCommandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(immediateCommandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &immediateCommandBuffer));

	mainDeletionQueue.push_command_pool(immediateCommandPool);
}

void VulkanEngine::init_sync_structures() {
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &frames[i].renderFence));

		VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frames[i].swapchainSemaphore));
	}

	VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &immediateFence));


	mainDeletionQueue.push_fence(immediateFence);
}


void VulkanEngine::create_swapchain(uint32_t width, uint32_t height) {

	VkBool32 res;
	vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsQueueFamily, surface, &res);
	if (res != VK_TRUE)
	{
		fprintf(stderr, "Error no WSI support on physical device 0\n");
		exit(-1);
	}

	vkb::SwapchainBuilder swapchainBuilder{ physicalDevice, device, surface };

	swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	auto result = swapchainBuilder
		.set_desired_format(VkSurfaceFormatKHR{ .format = swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build();

	if (!result) {
		fmt::print("failed to create swapchain {}\n", result.error().message());
		return;
	}

	vkb::Swapchain vkbSwapchain = result.value();

	swapchainExtent = vkbSwapchain.extent;
	swapchain = vkbSwapchain.swapchain;
	swapchainImages = vkbSwapchain.get_images().value();
	swapchainImageViews = vkbSwapchain.get_image_views().value();
	swapchainImageCount = swapchainImages.size();
	//fmt::print("swapchainImage amount : {}\n", swapchainImageCount);

	swapchainImageRenderSemaphores.resize(swapchainImageCount);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();
	for (auto& semaphore : swapchainImageRenderSemaphores) {
		VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphore));
	}
	
	if (FRAME_OVERLAP > swapchainImageCount) {
		fmt::print("Warning: FRAME_OVERLAP is greater than swapchain image count!\n");
	}
}

void VulkanEngine::create_offscreen_resources() {

	VkExtent3D drawImageExtent = {
		windowExtent.width,
		windowExtent.height,
		1
	};

	drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	drawImage.imageExtent = drawImageExtent;

	VmaAllocationCreateInfo rimg_allocinfo = {};
	rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimg_info = vkinit::image_create_info(drawImage.imageFormat, drawImageUsages, drawImageExtent);

	vmaCreateImage(vmaAllocator, &rimg_info, &rimg_allocinfo, &drawImage.image, &drawImage.allocation, nullptr);

	VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(drawImage.imageFormat, drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(device, &rview_info, vkAllocator, &drawImage.imageView));


	depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	depthImage.imageExtent = drawImageExtent;
	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkImageCreateInfo dimg_info = vkinit::image_create_info(depthImage.imageFormat, depthImageUsages, drawImageExtent);

	vmaCreateImage(vmaAllocator, &dimg_info, &rimg_allocinfo, &depthImage.image, &depthImage.allocation, nullptr);

	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(depthImage.imageFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);


	VK_CHECK(vkCreateImageView(device, &dview_info, nullptr, &depthImage.imageView));


	

	mainDeletionQueue.push_offscreen_image(drawImage);
	mainDeletionQueue.push_offscreen_image(depthImage);

}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
	VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;

	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(vmaAllocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation,
		&newBuffer.info));

	return newBuffer;
}

void VulkanEngine::immediateCommandSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VK_CHECK(vkResetFences(device, 1, &immediateFence));
	VK_CHECK(vkResetCommandBuffer(immediateCommandBuffer, 0));

	VkCommandBuffer cmd = immediateCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
	VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

	// submit command buffer to the queue and execute it.
	//  _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, immediateFence));

	VK_CHECK(vkWaitForFences(device, 1, &immediateFence, true, 9999999999));
}


void VulkanEngine::destroy_swapchain() {
	
	if (device == VK_NULL_HANDLE) {
		return; 
	}

	for (auto& imageRenderSemaphore : swapchainImageRenderSemaphores) {
		vkDestroySemaphore(device, imageRenderSemaphore, vkAllocator);
	}

	for (auto& imageView : swapchainImageViews) {
		if (imageView != VK_NULL_HANDLE) {
			vkDestroyImageView(device, imageView, vkAllocator);
			imageView = VK_NULL_HANDLE;
		}
	}
	swapchainImageViews.clear(); 

	if (swapchain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(device, swapchain, vkAllocator);
		swapchain = VK_NULL_HANDLE;
	}
}


GPUMeshBuffers VulkanEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) {
	//> mesh_create_1
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	GPUMeshBuffers newSurface;

	//create vertex buffer
	newSurface.vertexBuffer = create_buffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	//find the adress of the vertex buffer
	VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.vertexBuffer.buffer };
	newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(device, &deviceAdressInfo);

	//create index buffer
	newSurface.indexBuffer = create_buffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	//< mesh_create_1
	// 
	//> mesh_create_2
	AllocatedBuffer staging = create_buffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	void* data = staging.allocation->GetMappedData();

	// copy vertex buffer
	memcpy(data, vertices.data(), vertexBufferSize);
	// copy index buffer
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

	immediateCommandSubmit([&](VkCommandBuffer cmd) {
		VkBufferCopy vertexCopy{ 0 };
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy{ 0 };
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
		});

	vmaDestroyBuffer(vmaAllocator, staging.buffer, staging.allocation);

	return newSurface;
}





