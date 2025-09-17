#pragma once 
#include "vk_types.h"
#include "vk_util.h"
#include "vk_renderer.h"


class PipelineBuilder {
public:

	std::unique_ptr<PipelineResource> res = std::make_unique<PipelineResource>();

	PipelineBuilder() { clear(); }

	void clear();

	VkPipeline build_pipeline(VkDevice device, RenderMode mode, const std::optional<VkRenderPass>& renderPass = std::nullopt, PipelineResource* storeRes = nullptr);

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

	
};
