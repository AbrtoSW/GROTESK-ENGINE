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
#include <glm/gtc/quaternion.hpp>
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
	VkDeviceSize sizeBytes = 0;
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
	uint32_t indexCountTotal = 0;
};

struct PC_TerrainBuild {
	uint32_t firstIndex;
	uint32_t indexCount;
	int32_t  vertexOffset;
	uint32_t tileCount;   
};

struct GPUDrawPushConstants {
	glm::mat4 worldMatrix;
	VkDeviceAddress vertexBuffer;
};


struct GPUSceneData {
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
	glm::vec4 cameraPos;
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

enum struct SharedLayout {
	Uninitialized,
	False,
	True,
	
};

enum struct LayoutOwnership {
	Uninitialized,
	False,
	True
};


using LayoutID = size_t;
using PipelineID = size_t;

struct ShaderFile {

	std::string path;

	ShaderFile() = default;

	explicit ShaderFile(const std::string& file)
		: path(file) {
	}
};


struct PipelineLayoutResource {
	VkPipelineLayout layout;
	SharedLayout isShared = SharedLayout::False;
	LayoutOwnership isOwned = LayoutOwnership::True;
	LayoutID pipelineLayoutID;
};

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
	

	std::vector<VkDescriptorSetLayout> setLayouts;
};

enum PipelineType {
	Uninitialized,
	Graphics,
	Compute,
};
//might change this with pipelineRes
struct OldPipelineResource {

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
	PipelineID pipelineID;
	PipelineLayoutResource pipelineLayout;
	Shader shaderType;

	PipelineType type = Uninitialized;

private:
	std::variant<BaseGraphicsPipelineConfig, std::unique_ptr<BaseGraphicsPipelineConfig>> config;
};

using PID = uint32_t;

struct PipelineRes {
	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkPipelineLayout pLayout{ VK_NULL_HANDLE };
	Shader shaderType;
	const char* name{ nullptr };
	PipelineType type = Uninitialized;

	BaseGraphicsPipelineConfig* getGraphicsConfig() {
		if (std::holds_alternative<BaseGraphicsPipelineConfig>(config)) {
			return &std::get<BaseGraphicsPipelineConfig>(config);
		}
		else {
			//for polymorphism we'll see if i develop this further for now ill keep this
			return std::get<std::unique_ptr<BaseGraphicsPipelineConfig>>(config).get();
		}
	}

private:
	std::variant<BaseGraphicsPipelineConfig, std::unique_ptr<BaseGraphicsPipelineConfig>> config;
};



struct MaterialInstance {
	VkPipeline* pipeline;
	VkDescriptorSet materialSet;

	MaterialPass passType;
};

enum struct Hotloadable {
	False,
	True
};

struct GPUDrawData {
	VkBuffer        indexBuffer;
	VkDeviceSize    indexBufferSizeBytes;  // must be set as above
	uint32_t        firstIndex;
	uint32_t        indexCount;
	VkDeviceAddress vertexBufferAddress;
	glm::mat4       transform;
	uint32_t        instanceCount = 1;
}; 


struct RenderContext {
	const GPUSceneData* sceneData;
	VkDescriptorSet globalSet;
};


struct DescriptorBundle {
	std::array<VkDescriptorSet, 4> sets{};
	uint32_t presentMask{ 0 };

	uint8_t  dynCountPerSet[4]{ 0,0,0,0 };
	std::array<uint32_t, 16> dynOffsets{};
	uint32_t dynTotal{ 0 };

	// ---- Convenience API ----

	void clear() {
		sets.fill(VK_NULL_HANDLE);
		presentMask = 0;
		std::fill(std::begin(dynCountPerSet), std::end(dynCountPerSet), 0);
		dynOffsets.fill(0);
		dynTotal = 0;
	}

	void add(uint32_t setIndex, VkDescriptorSet set) {
		if (setIndex >= sets.size()) return;
		sets[setIndex] = set;
		presentMask |= (1u << setIndex);
	}

	void addDynamic(uint32_t setIndex,
		VkDescriptorSet set,
		std::initializer_list<uint32_t> offsets)
	{
		if (setIndex >= sets.size()) return;
		sets[setIndex] = set;
		presentMask |= (1u << setIndex);

		dynCountPerSet[setIndex] = static_cast<uint8_t>(offsets.size());
		for (uint32_t off : offsets)
			dynOffsets[dynTotal++] = off;
	}

	bool has(uint32_t setIndex) const {
		return (presentMask & (1u << setIndex)) != 0;
	}
};

struct RenderItem {
	GPUDrawData	gpuData;
	PID	pID;
	DescriptorBundle  desc;
};

struct VertexP2 {
	glm::vec2 position;
};


struct alignas(8) TerrainInstance {
	glm::vec2 originXZ;
	glm::vec2 uvOrigin;
	glm::vec2 uvScale;
	float     worldScale;
	uint32_t  tileIndex;
	uint32_t  _pad[2];
};

// fits your naming; pass per-click/drag
struct BrushStroke {
	uint32_t layer;        // cache slot / array layer for the TileID under cursor
	glm::vec2 tileOrigin;  // world XZ origin of that tile (meters)
	float     worldScale;  // meters per texel (your per-tile worldScale)
	glm::vec2 worldXZ;     // brush center in world XZ
	glm::vec2 localUV;
	float     radiusWorld; // meters
	float     strength;    // meters (+raise, -lower)
	int       mode;        // 0=Add/Lower (you can extend later)
};

struct EditorBrushState {
	float radiusWorld = 12.0f;
	float strength = 40.f;
	int   mode = 0; // Add/Lower
};

struct BrushStageCtx {
	AllocatedBuffer stage;
	uint32_t layer;
	int x0, y0, x1, y1;
	uint32_t W, K;
	VkDeviceSize bytes;
	bool isCoherent;
	float cx, cy, rTex;
};

struct BrushUBO {
	glm::vec2 centerXZ; // meters
	float     radiusM;  // meters
	float     edgeM;    // meters
	glm::vec4 color;
	float     opacity;

	// NEW: keep globals here so both CPU & GPU paths stay in sync
	float     strength; // meters (+raise, -lower)
	int       mode;     // 0 = Add/Lower
	int       _pad;     // align to 16 bytes (UBO-friendly)
};
