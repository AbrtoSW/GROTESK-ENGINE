#pragma once
#include <vk_types.h>
#include "vk_descriptors.h"
#include "vk_renderer.h"
#include "vkbootstrap/VkBootstrap.h"
#include "vk_util.h"




class Renderer;


class VulkanEngine {
public:

	bool isInitialized{ false };
	int frameNumber{ 0 };
	bool stop_rendering{ false };

	VmaAllocator vmaAllocator;
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug_messenger;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	VkSurfaceKHR surface;
	DeletionQueue mainDeletionQueue;

	VkAllocationCallbacks* vkAllocator = nullptr;

	//handles vulkan extensions
	std::vector<const char*> extensions;

	//sdl window handles
	VkExtent2D windowExtent{ 1700 , 900 };
	struct SDL_Window* window{ nullptr };
	float main_scale;
	bool resize_requested = false;
	bool hotload_requested = false;

	//frame handles header

	FrameData frames[FRAME_OVERLAP];
	FrameData& get_current_frame() { return frames[frameNumber % FRAME_OVERLAP]; };

	

	//queue
	VkQueue graphicsQueue;
	uint32_t graphicsQueueFamily;

	// immediate command submit handles
	VkFence immediateFence;
	VkCommandBuffer immediateCommandBuffer;
	VkCommandPool immediateCommandPool;

	//swapchain handles 
	VkSwapchainKHR swapchain;
	VkFormat swapchainImageFormat;
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	uint32_t swapchainImageCount;
	VkExtent2D swapchainExtent;
	std::vector<VkSemaphore> swapchainImageRenderSemaphores;


	// first image we draw into which gets blited into swapchain
	AllocatedImage drawImage;
	AllocatedImage depthImage;


	Renderer* renderer{ nullptr };


	static VulkanEngine* loadedEngine;
	static VulkanEngine& Get();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//frame loop
	void render_frame();

	//run main loop
	void run();


	void immediateCommandSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);



private:
	friend class Renderer;
	bool init_SDL3();
	void init_vulkan();
	void init_swapchain_resources();
	void init_commands();
	void init_sync_structures();
	void create_swapchain(uint32_t width, uint32_t height);
	void create_offscreen_resources();
	void destroy_swapchain();

};

