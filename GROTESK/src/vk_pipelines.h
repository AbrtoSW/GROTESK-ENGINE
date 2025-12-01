#pragma once 
#include "vk_types.h"
#include "vk_util.h"
#include <unordered_set>
class VulkanEngine;


class NewPipelineManager {
public:

	inline static PID nextID() {
		return pID++;
	}

	PID registerPipeline(PipelineRes& pRes, Hotloadable hotload, std::optional<const char*> name = std::nullopt, bool autoDestruction = true);
	void showInfo();
	VkPipeline getPipeline(PID id) const;
	VkPipelineLayout getLayout(PID id) const;
	auto& get_shaderMap() { return shaderMap; }

private:
	std::unordered_map<PID, PipelineRes> pipelineStorage;
	std::unordered_set<VkPipeline>	queuedPipelines;
	std::unordered_set<VkPipelineLayout> queuedLayouts;
	std::unordered_map<std::string, std::vector<PipelineRes*>> shaderMap;
	
	inline static PID pID = 0;

	void track_shaders_for_hotload(PipelineRes& resource);

};



class PipelineBuilder {
public:

	std::unique_ptr<PipelineRes> res = std::make_unique<PipelineRes>();

	PipelineBuilder() { clear(); }

	void clear();

	VkPipeline build_pipeline(VkDevice device, RenderMode mode,  PipelineRes* storeRes = nullptr);

	void set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
	void set_polygon_mode(VkPolygonMode mode);
	void set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace);
	void set_multisampling_none();
	void disable_blending();
	void set_color_attachment_format(VkFormat format);
	void set_depth_format(VkFormat format);
	void disable_depthtest();
	void enable_depthtest(bool depthWriteEnable, VkCompareOp op);
	void set_input_topology(VkPrimitiveTopology topology);
	void enable_blending_additive();
	void enable_blending_alphablend();
	void set_renderpass(VkRenderPass renderpass);


	
};
