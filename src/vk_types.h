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
#include <variant>

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <set>

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

enum class RenderMode {
	Classic,
	Dynamic,
};


using LayoutID = size_t;
using PipelineID = size_t;
using ShaderFile = std::string;

struct ShaderInfo {
	ShaderFile file;
	VkShaderStageFlagBits stage;
	std::filesystem::file_time_type lastModified;
};

struct Shader {
	ShaderInfo vertexShader;
	ShaderInfo geometryShader;
	ShaderInfo fragmentShader;
	ShaderInfo computeShader;
};

struct BaseGraphicsPipelineConfig {
	// i could make a derived config for the specialized pipeline types that will need other stuff like shadows but since shadows dont use some of these i might not, but this is only for hotloading so it shouldnt really matter for 
	VkPipelineRenderingCreateInfo renderInfo;

	VkPipelineLayoutCreateInfo layoutInfo;
	VkPushConstantRange pushConstantRange;

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	VkPipelineRasterizationStateCreateInfo rasterizer;
	VkPipelineMultisampleStateCreateInfo multisampling;
	VkPipelineDepthStencilStateCreateInfo depthStencil;
	VkPipelineColorBlendAttachmentState colorBlendAttachment;
	VkFormat colorAttachmentformat;


	VkPipelineDynamicStateCreateInfo dynamicStateInfo;
	std::vector<VkDynamicState> dynamicStates;

	VkPipelineViewportStateCreateInfo viewportStateInfo;
	VkPipelineVertexInputStateCreateInfo vertexInputInfo;
	VkPipelineColorBlendStateCreateInfo colorBlendingInfo;

	VkRenderPass renderPass;
	RenderMode renderMode;
};

enum PipelineType {
	Uninitialized,
	Graphics,
	Compute,
};

struct PipelineResource {

	BaseGraphicsPipelineConfig* getGraphicsConfig() {
		if (std::holds_alternative<BaseGraphicsPipelineConfig>(config)) {
			return &std::get<BaseGraphicsPipelineConfig>(config);
		}
		else {
			//for polymorphism we'll see if i develop this further for now ill keep this
			return std::get<std::unique_ptr<BaseGraphicsPipelineConfig>>(config).get();
		}
	}

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
	Shader shader;
	LayoutID pipelineLayoutID;
	PipelineID pipelineID;
	PipelineType type = Uninitialized;

private:
	std::variant<BaseGraphicsPipelineConfig, std::unique_ptr<BaseGraphicsPipelineConfig>> config;
};

struct MaterialInstance {
	PipelineResource* pipeline;
	VkDescriptorSet materialSet;
	MaterialPass passType;
};

enum struct TrackShader {
	No,
	Yes
};