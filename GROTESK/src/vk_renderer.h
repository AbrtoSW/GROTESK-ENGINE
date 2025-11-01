#pragma once
#include "vk_types.h"
#include "vk_descriptors.h"
#include "vk_loader.h"
#include "vk_util.h"
#include "vk_pipelines.h"


class Renderer;

struct GLTFMetallic_Roughness {

	VkDescriptorSetLayout materialLayout;

	struct MaterialConstants {
		glm::vec4 colorFactors;
		glm::vec4 metal_rough_factors;
		// padding, we need it anyway for uniform buffers
		uint32_t colorTexID;
		uint32_t metalRoughTexID;
		uint32_t pad1;
		uint32_t pad2;
		glm::vec4 extra[13];
	};

	struct MaterialResources {
		AllocatedImage colorImage;
		VkSampler colorSampler;
		AllocatedImage metalRoughImage;
		VkSampler metalRoughSampler;
		VkBuffer dataBuffer;
		uint32_t dataBufferOffset;
	};


	void build_pipelines(VulkanEngine* engine, Renderer* renderer);
	void clear_resources(VkDevice device);

	MaterialInstance write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator, Renderer* renderer);
};

struct TextureID {
	uint32_t Index;
};

struct TextureCache {

	std::vector<VkDescriptorImageInfo> Cache;
	std::unordered_map<std::string, TextureID> NameMap;
	TextureID AddTexture(const VkImageView& image, VkSampler sampler);
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

	VkExtent2D drawExtent{};

	VkDescriptorSetLayout gpuSceneDataDescriptorLayout;
	VkDescriptorSetLayout terrainInstanceDescriptorLayout;

	VkDescriptorSet terrainSet;
	VkRenderPass drawImageRenderPass = VK_NULL_HANDLE;

	// made it so polymorphism is still enabled for hotloading but specific pipelines can be made so there isnt heap overhead 
	OldPipelineResource meshPipeline;

	TextureCache texCache;


	NewPipelineManager newManager;
	LayoutID gradientPipelineLayoutID;
	PipelineID gradientPipelineID;
	PipelineID skyPipelineID;

	MaterialInstance materialData;
	GLTFMetallic_Roughness metalRoughMaterial;

	std::vector<std::shared_ptr<MeshAsset>> testMeshes;

	PipelineRes newOpaquePipeline;
	PipelineRes newTransparentPipeline;
	PID pidNewOpaquePipeline;
	PID pidNewTransparentPipeline;

	PID pidHeightMap;
	PipelineRes heightMapPipeline;

	DescriptorAllocatorGrowable globalDescriptorAllocator{};

	AllocatedBuffer terrainInstanceBuffer;
	uint32_t terrainInstanceCount = 0;
	AllocatedBuffer terrainIndirectDraws;
	AllocatedBuffer terrainDrawCount;
	PipelineRes terrainCompute;
	VkDescriptorSetLayout terrainBuildDescriptorLayout;
	VkDescriptorSet terrainBuildSet;
	GPUMeshBuffers terrainGrid;
	bool terrainIndirectBuiltThisFrame = false;

	
	VkDescriptorSet heightmapTexSet;
	VkDescriptorSet heightmapDiffuseSet;

	AllocatedImage heightmapImage;
	AllocatedImage heightmapDiffuseImage;
	VkSampler heightmapSampler;
	VkSampler diffuseSampler;
	AllocatedBuffer brushUBO;
	// meters per grid step (shared between terrain + editor brush)
	float terrainGridWorldScale = 1.0f;
	float heightAmplitudeMeters = 100.0f;
	std::vector<BrushStroke> editorBrushQueue;

	PipelineRes terrainBrushCompute;                 // compute pipeline for GPU brush
	VkDescriptorSetLayout terrainBrushDescriptorLayout = VK_NULL_HANDLE;
	VkDescriptorSet terrainBrushSet = VK_NULL_HANDLE;
	// engine functions
	void init_renderer();
	void init_renderer_cleanup();
	void render_frame(const std::vector<RenderItem>& items, const RenderContext& context);

	void init_framebuffers();
	void init_descriptors();
	
	VkPipeline rebuild(VkDevice device, PipelineRes& res);

	void cpu_brush_small(VkCommandBuffer cmd, const BrushStroke& b);
	void editor_apply_cpu_brush(VkCommandBuffer cmd, const glm::vec2& worldXZ, uint32_t layer, const glm::vec2& tileOrigin, float worldScale, float radiusWorld, float strength, int mode);
	void cpu_brush_stage_read(VkCommandBuffer cmd, const BrushStroke& b, BrushStageCtx& ctx);
	void cpu_brush_stage_cpu_modify(const BrushStroke& b, BrushStageCtx& ctx);
	void cpu_brush_stage_write(VkCommandBuffer cmd, const BrushStageCtx& ctx);
	void HotloadShader();

	void cpu_draw(VkCommandBuffer cmd,VkPipelineLayout currentLayout, const RenderItem& items);

	void init_terrain_gpu_resources(uint32_t maxTiles, bool useExternalHM,bool useTexture);
	void init_terrain_brush_pipeline();
	void upload_terrain_instances(const TerrainInstance* data, uint32_t count);

	void update_brush_ubo(const glm::vec2& centerWorldXZ, float radiusMeters, float strength, int mode);
	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image_array(VkExtent3D size, uint32_t layers, VkFormat format, VkImageUsageFlags usage, bool mipmapped);
	void upload_to_image_array_layer(AllocatedImage& dstArray, uint32_t layer, void* data, VkExtent3D size, VkFormat format, bool mipmapped);
	void clear_image_array_layer(AllocatedImage& dstArray, uint32_t layer, VkFormat format, float value/*0..1*/);
private:
	VulkanEngine& engine;

	// Render-related resources
	VkRenderPass swapchainRenderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> swapchainFrameBuffers = {};
	VkFramebuffer drawImageFrameBuffer = VK_NULL_HANDLE;
	
	VkDescriptorSet drawImageDescriptors = VK_NULL_HANDLE;
	VkDescriptorSetLayout drawImageDescriptorLayout = VK_NULL_HANDLE;
	
	VkDescriptorSetLayout singleImageDescriptorLayout;

	GPUSceneData sceneData;

	VkDescriptorPool imguiPool = VK_NULL_HANDLE;


	void init_draw_image_renderpass(VkCommandBuffer cmd, const std::vector<RenderItem>& items, const RenderContext& context);
	void init_swapchain_renderpass(VkCommandBuffer cmd, uint32_t imageIndex);

	void init_pipelines();

	void init_terrain_mesh_pipeline();
	void init_terrain_compute_pipeline();
	void init_default_data();
	void terrain_build_indirect(VkCommandBuffer cmd);
	void gpu_brush_apply(VkCommandBuffer cmd, const BrushStroke& b);
	void gpu_draw(VkCommandBuffer cmd, const RenderContext& context);
	void render_pass_geometry(VkCommandBuffer cmd, const std::vector<RenderItem>& items, const RenderContext& context);
	void init_imgui();


	void render_imgui(VkCommandBuffer cmd);

	
	void render_dynamic_imgui(VkCommandBuffer cmd, VkImageView targetImageView);


	void create_draw_image_renderpass();
	void create_swapchain_renderpass();

	void create_draw_image_framebuffer();
	void create_swapchain_framebuffer();


};

