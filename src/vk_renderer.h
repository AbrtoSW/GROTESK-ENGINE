#pragma once
#include "vk_types.h"
#include "vk_descriptors.h"
#include "vk_loader.h"
#include "util.h"

struct GLTFMetallic_Roughness {
	MaterialPipeline opaquePipeline;
	MaterialPipeline transparentPipeline;

	VkDescriptorSetLayout materialLayout;

	struct MaterialConstants {
		glm::vec4 colorFactors;
		glm::vec4 metal_rough_factors;
		//padding, we need it anyway for uniform buffers
		glm::vec4 extra[14];
	};

	struct MaterialResources {
		AllocatedImage colorImage;
		VkSampler colorSampler;
		AllocatedImage metalRoughImage;
		VkSampler metalRoughSampler;
		VkBuffer dataBuffer;
		uint32_t dataBufferOffset;
	};

	DescriptorWriter writer;

	void build_pipelines(VulkanEngine* engine);
	void clear_resources(VkDevice device);

	MaterialInstance write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator);
};

class VulkanEngine;

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


struct GraphicsPipelineConfig {
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

struct ComputePipelineConfig {

	VkPipelineLayout computePipelineLayout;
	VkPipelineLayoutCreateInfo computePipelineLayoutInfo;

	VkPushConstantRange pushConstant;

	VkPipelineShaderStageCreateInfo shaderStageInfo;
	VkComputePipelineCreateInfo computePipelineCreateInfo;

	ComputeEffect computeEffect;
	ComputeEffect otherComputeEffect;

};

enum PipelineType {
	Graphics,
	Compute,
};



struct BasePipelineResource {

public: 

	virtual ~BasePipelineResource() = default;

	virtual GraphicsPipelineConfig* getGraphicsConfig() { return nullptr; }
	virtual ComputePipelineConfig* getComputeConfig() { return nullptr; }


	virtual VkPipeline rebuild(VkDevice device, BasePipelineResource& res) = 0;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
	Shader shader;
	LayoutID pipelineLayoutID;
	PipelineID pipelineID;
	PipelineType type;

};



struct GraphicsPipelineResource : BasePipelineResource {

	GraphicsPipelineResource() = default;

	GraphicsPipelineResource(std::vector<BasePipelineResource*>& deletionQueue) {
		deletionQueue.push_back(this);
	}

	virtual ~GraphicsPipelineResource() override {}

	GraphicsPipelineConfig* getGraphicsConfig() override { return &config; }
	VkPipeline rebuild(VkDevice device, BasePipelineResource& res) override;

	GraphicsPipelineConfig config;
};


struct ComputePipelineResource : BasePipelineResource {

	ComputePipelineResource() = default;

	ComputePipelineResource(std::vector<BasePipelineResource*>& deletionQueue) {
		deletionQueue.push_back(this);
	}

	virtual ~ComputePipelineResource() override {}

	ComputePipelineConfig* getComputeConfig() override { return &config; }
	VkPipeline rebuild(VkDevice device, BasePipelineResource& res) override;

	ComputePipelineConfig config;
};





struct PipelineManager {

public: 
	friend class Renderer;


	static void init_PipelineCache();
	void static destroyPipelineCache();

	inline static LayoutID createLayoutID() { return nextLayoutID++; }
	inline static PipelineID createPipelineID() { return nextPipelineID++; }

	inline static auto& get_shaderMap() { return shaderMap; }


	void init_pipeline_resource(BasePipelineResource& res);
	//temp function want to move to full init_pipeline_resource function 
	void store_pipeline(PipelineID pID, LayoutID lID, VkPipeline pipeline, VkPipelineLayout layout);
	void link_shader(BasePipelineResource& resource);
	VkPipelineLayout get_layout(LayoutID id) const;
	VkPipeline get_pipeline(PipelineID id) const;

	inline static VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	inline static LayoutID nextLayoutID = 1;
	inline static PipelineID nextPipelineID = 1;
	inline static std::unordered_map<std::string, std::vector<BasePipelineResource*>> shaderMap;



	std::unordered_map<LayoutID, VkPipelineLayout> layoutLookup;
	std::unordered_map<PipelineID, VkPipeline> pipelineLookup;
};


class Renderer {

public:

	Renderer(VulkanEngine& engine);
	~Renderer();

	//imgui
	std::vector<ComputeEffect> backgroundEffects;
	int currentBackgroundEffect{ 0 };

	//testing
	GPUMeshBuffers rectangle;

	AllocatedImage whiteImage;
	AllocatedImage blackImage;
	AllocatedImage greyImage; 
	AllocatedImage errorCheckerBoardImage;

	VkSampler defaultSamplerLinear;
	VkSampler defaultSamplerNearest;

	std::vector<BasePipelineResource*> resourceInstanceDeletion;
	BasePipelineResource* meshPipeline = new GraphicsPipelineResource(resourceInstanceDeletion);
	
	BasePipelineResource* computePipeline = new ComputePipelineResource(resourceInstanceDeletion);

	LayoutID gradientPipelineLayoutID;
	PipelineID gradientPipelineID;
	PipelineID skyPipelineID;

	// engine functions
	void init_renderer();
	void init_renderer_cleanup();
	void render_frame();

	void init_framebuffers();
	void init_descriptors();
	
	void HotloadShader();

private:

	VulkanEngine& engine;


	// Render-related resources
	VkRenderPass swapchainRenderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> swapchainFrameBuffers = {};
	VkRenderPass drawImageRenderPass = VK_NULL_HANDLE;
	VkFramebuffer drawImageFrameBuffer = VK_NULL_HANDLE;
	VkExtent2D drawExtent{};
	
	VkDescriptorSet drawImageDescriptors = VK_NULL_HANDLE;
	VkDescriptorSetLayout drawImageDescriptorLayout = VK_NULL_HANDLE;
	
	VkDescriptorSetLayout singleImageDescriptorLayout;

	GPUSceneData sceneData;
	VkDescriptorSetLayout gpuSceneDataDescriptorLayout;

	DescriptorAllocatorGrowable globalDescriptorAllocator{};
	VkDescriptorPool imguiPool = VK_NULL_HANDLE;

	PipelineManager managePipeline;

	std::vector<std::shared_ptr<MeshAsset>> testMeshes;



	void init_dynamic_rendering(VkCommandBuffer cmd);
	void init_dynamic_rendering(VkCommandBuffer cmd, VkImageView targetImageView);

	void init_draw_image_renderpass(VkCommandBuffer cmd);
	void init_swapchain_renderpass(VkCommandBuffer cmd, uint32_t imageIndex);

	void init_pipelines();

	void init_backgound_pipelines();
	void init_triangle_pipeline();
	void init_mesh_pipeline();
	void init_default_data();
	void render_pass_geometry(VkCommandBuffer cmd);
	void init_imgui();


	void render_imgui(VkCommandBuffer cmd);

	
	void render_dynamic_imgui(VkCommandBuffer cmd, VkImageView targetImageView);
	void render_background(VkCommandBuffer cmd);



	void create_draw_image_renderpass();
	void create_swapchain_renderpass();

	void create_draw_image_framebuffer();
	void create_swapchain_framebuffer();

	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);

};