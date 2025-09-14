#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>
#include <unordered_map>
#include <filesystem>
#include <chrono>


#include "backends/imgui_impl_vulkan.h"
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vma/vk_mem_alloc.h>
#include <fmt/core.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <set>



struct BasePipelineResource;

class VulkanEngine;
class DescriptorAllocatorGrowable;

#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            fmt::println("Detected Vulkan error: {}", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
    } while (0)

struct ComputePushConstants {
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct ComputeEffect {
	const char* name;
	VkPipeline pipeline;
	VkPipelineLayout layout;
	ComputePushConstants data;
};



struct AllocatedImage {
	VkImage image;
	VkImageView imageView;
	VmaAllocation allocation;
	VkExtent3D imageExtent;
	VkFormat imageFormat;
};

struct AllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
};

struct Vertex {
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
};

struct GPUMeshBuffers {

	AllocatedBuffer indexBuffer;
	AllocatedBuffer vertexBuffer;
	VkDeviceAddress vertexBufferAddress;
};


struct GPUDrawPushConstants {
	glm::mat4 worldMatrix;
	VkDeviceAddress vertexBuffer;
};


struct GPUSceneData {
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection; // w = intensity
	glm::vec4 sunlightColor;
};

enum class MaterialPass :uint8_t {
	MainColor,
	Transparent,
	Other
};
struct MaterialPipeline {
	VkPipeline pipeline;
	VkPipelineLayout layout;
};

struct MaterialInstance {
	MaterialPipeline* pipeline;
	VkDescriptorSet materialSet;
	MaterialPass passType;
};




struct DeletionQueue {
private:
	std::vector<AllocatedBuffer> vmaAllocatedBuffer;
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	std::vector<VkDescriptorPool> descriptorPools;
	std::vector<VkSampler> samplers;
	std::vector<AllocatedImage> offscreenImages;
	std::vector<AllocatedImage> allocatedImages;
	std::vector<VkCommandPool> commandPool;
	std::vector<VkFence> fences;
	std::vector<VkRenderPass> renderpass;
	std::vector<VkFramebuffer> framebuffer;
	std::vector<VkPipelineLayout> pipelineLayouts;
	


public:

	std::vector<VkPipeline> pipelines;

	std::deque<std::function<void()>> deletors;

	void push_deletion_lambda(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush_deletion_lambda() {
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)();
		}
		deletors.clear();
	}

	void flushFrameResources(VmaAllocator& vmaAllocator) {
		for (auto& b : vmaAllocatedBuffer) {
			vmaDestroyBuffer(vmaAllocator, b.buffer, b.allocation);
		}
	}

	void flushMainResources(VkDevice device, VmaAllocator& vmaAllocator) {


		for (auto& s : samplers) {
			vkDestroySampler(device, s, nullptr);
		}

		for (auto& aImg : allocatedImages) {
			
			vkDestroyImageView(device, aImg.imageView, nullptr);
			vmaDestroyImage(vmaAllocator, aImg.image, aImg.allocation);
		}

		for (auto& offs : offscreenImages) {
			vkDestroyImageView(device, offs.imageView, nullptr);
			vmaDestroyImage(vmaAllocator, offs.image, offs.allocation);
		}

		for (auto& b : vmaAllocatedBuffer) {
			vmaDestroyBuffer(vmaAllocator, b.buffer, b.allocation);
		}

		ImGui_ImplVulkan_Shutdown();

		for (auto& d : descriptorSetLayouts) {
			vkDestroyDescriptorSetLayout(device, d, nullptr);
		}

		for (auto& p : descriptorPools) {
			fmt::print("destroyed pool amount in engine cleanup is {}\n", (void*)p);
			vkDestroyDescriptorPool(device, p, nullptr);
		}


		for (auto& p : pipelines) {
			vkDestroyPipeline(device, p, nullptr);
		}

		for (auto& l : pipelineLayouts) {
			vkDestroyPipelineLayout(device, l, nullptr);
		}


		for (auto& f : framebuffer) {
			vkDestroyFramebuffer(device, f, nullptr);
		}

		for (auto& r : renderpass) {
			vkDestroyRenderPass(device, r, nullptr);
		}

		for (auto& f : fences) {
			vkDestroyFence(device, f, nullptr);
		}

		for (auto& c : commandPool) {
			vkDestroyCommandPool(device, c, nullptr);
		}

		vmaDestroyAllocator(vmaAllocator);

	}

	void resizeFlush(VkDevice device, VmaAllocator& vmaAllocator) {

		for (auto& offs : offscreenImages) {
			if (offs.imageView != VK_NULL_HANDLE) {
				vkDestroyImageView(device, offs.imageView, nullptr);
				offs.imageView = VK_NULL_HANDLE;
			}
			if (offs.image != VK_NULL_HANDLE) {
				vmaDestroyImage(vmaAllocator, offs.image, offs.allocation);
				offs.image = VK_NULL_HANDLE;
			}
		}
		for (auto& fb : framebuffer) {
			if (fb != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(device, fb, nullptr);
				fb = VK_NULL_HANDLE;
			}
		}
	}



	template <typename T>
	inline void push_sampler(const T& s) {
		VulkanEngine::Get().mainDeletionQueue.samplers.push_back(s);
	}

	template <typename T>
	inline void push_allocated_image(const T& image) {
		VulkanEngine::Get().mainDeletionQueue.allocatedImages.push_back(image);
	}

	template <typename T>
	inline void push_offscreen_image(const T& image) {
		VulkanEngine::Get().mainDeletionQueue.offscreenImages.push_back(image);
	}

	template <typename T>
	inline void push_allocated_buffer(const T& buffer) {
		VulkanEngine::Get().mainDeletionQueue.vmaAllocatedBuffer.push_back(buffer);
	}

	template <typename T>
	inline void push_descriptor_set_layout(T layout) {
		VulkanEngine::Get().mainDeletionQueue.descriptorSetLayouts.push_back(layout);
	}

	template <typename T>
	inline void push_descriptor_pool(T pool) {
		VulkanEngine::Get().mainDeletionQueue.descriptorPools.push_back(pool);
	}
	template <typename T>
	inline void push_command_pool(T pool) {
		VulkanEngine::Get().mainDeletionQueue.commandPool.push_back(pool);
	}
	template <typename T>
	inline void push_fence(T fence) {
		VulkanEngine::Get().mainDeletionQueue.fences.push_back(fence);
	}
	template <typename T>
	inline void push_renderpass(T rp) {
		VulkanEngine::Get().mainDeletionQueue.renderpass.push_back(rp);
	}
	template <typename T>
	inline void push_framebuffer(T fb) {
		VulkanEngine::Get().mainDeletionQueue.framebuffer.push_back(fb);
	}
	template <typename T>
	inline void push_pipeline(T pipe) {
		VulkanEngine::Get().mainDeletionQueue.pipelines.push_back(pipe);
	}
	template <typename T>
	inline void push_pipeline_layout(T layout) {
		VulkanEngine::Get().mainDeletionQueue.pipelineLayouts.push_back(layout);
	}

	template <typename T>
	void push_mesh_buffer_deletion(T& mesh) {
		// If mesh is a pointer/smart pointer, use ->meshBuffers
		if constexpr (requires { mesh->meshBuffers; }) {
			VulkanEngine::Get().mainDeletionQueue.vmaAllocatedBuffer.push_back(mesh->meshBuffers.indexBuffer);
			VulkanEngine::Get().mainDeletionQueue.vmaAllocatedBuffer.push_back(mesh->meshBuffers.vertexBuffer);
		}
		// Otherwise, assume mesh is already a GPUMeshBuffers object
		else {
			VulkanEngine::Get().mainDeletionQueue.vmaAllocatedBuffer.push_back(mesh.indexBuffer);
			VulkanEngine::Get().mainDeletionQueue.vmaAllocatedBuffer.push_back(mesh.vertexBuffer);
		}
	}
};


struct FrameData {
	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;
	VkSemaphore swapchainSemaphore;
	VkFence renderFence;
	DeletionQueue deletionQueue;
	std::unique_ptr<DescriptorAllocatorGrowable> frameDescriptors;
};

constexpr unsigned int FRAME_OVERLAP = 3;

enum class RenderMode {
	Classic,
	Dynamic,
};