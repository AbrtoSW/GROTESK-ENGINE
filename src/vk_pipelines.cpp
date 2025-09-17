#include "vk_pipelines.h"
#include "vk_initializers.h"
#include <fstream>


//see if you can just forward declare the renderer so its not the whole class 
#include "vk_renderer.h"


void PipelineBuilder::clear() {
	// clear all of the structs we need back to 0 with their correct stype

	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };

	graphicsResourceConfig->rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };

	graphicsResourceConfig->colorBlendAttachment = {};

	graphicsResourceConfig->multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };

	res->pipelineLayout = {};

	graphicsResourceConfig->depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

	graphicsResourceConfig->renderInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };

	graphicsResourceConfig->shaderStages.clear();
}



VkPipeline PipelineBuilder::build_pipeline(VkDevice device, RenderMode mode, const std::optional<VkRenderPass>& renderPass, PipelineResource* storeRes) {

	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->viewportStateInfo = {};
	graphicsResourceConfig->viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	graphicsResourceConfig->viewportStateInfo.pNext = nullptr;

	graphicsResourceConfig->viewportStateInfo.viewportCount = 1;
	graphicsResourceConfig->viewportStateInfo.scissorCount = 1;


	graphicsResourceConfig->colorBlendingInfo = {};
	graphicsResourceConfig->colorBlendingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	graphicsResourceConfig->colorBlendingInfo.pNext = nullptr;

	graphicsResourceConfig->colorBlendingInfo.logicOpEnable = VK_FALSE;
	graphicsResourceConfig->colorBlendingInfo.logicOp = VK_LOGIC_OP_COPY;
	graphicsResourceConfig->colorBlendingInfo.attachmentCount = 1;
	graphicsResourceConfig->colorBlendingInfo.pAttachments = &graphicsResourceConfig->colorBlendAttachment;

	 graphicsResourceConfig->vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

	VkGraphicsPipelineCreateInfo pipelineInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.pNext = &graphicsResourceConfig->renderInfo;

	pipelineInfo.stageCount = (uint32_t)graphicsResourceConfig->shaderStages.size();
	pipelineInfo.pStages = graphicsResourceConfig->shaderStages.data();
	pipelineInfo.pVertexInputState = &graphicsResourceConfig->vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &graphicsResourceConfig->inputAssembly;
	pipelineInfo.pViewportState = &graphicsResourceConfig->viewportStateInfo;
	pipelineInfo.pRasterizationState = &graphicsResourceConfig->rasterizer;
	pipelineInfo.pMultisampleState = &graphicsResourceConfig->multisampling;
	pipelineInfo.pColorBlendState = &graphicsResourceConfig->colorBlendingInfo;
	pipelineInfo.pDepthStencilState = &graphicsResourceConfig->depthStencil;
	pipelineInfo.layout = res->pipelineLayout;

	graphicsResourceConfig->renderMode = mode;
	
	if (graphicsResourceConfig->renderMode == RenderMode::Dynamic) {
		pipelineInfo.renderPass = VK_NULL_HANDLE;
		pipelineInfo.subpass = 0;
		graphicsResourceConfig->renderPass = VK_NULL_HANDLE;

		//later implement a more dynamic way to attach formats if for example you need to use only color formats or depth formats using the dynamic render option
		//it could be a std::optional so the pipline builder doesnt need so many parameters if not neccessary
		//make sure this works correctly, reminder once you want to add dynamic rendering to sync with classic renderpass
		//make sure you figure out how to pass depth information correctly from dynamic render pass -> to classic render pass
		if (graphicsResourceConfig->renderInfo.pColorAttachmentFormats || graphicsResourceConfig->renderInfo.depthAttachmentFormat == VK_FORMAT_UNDEFINED) {
			fmt::print("must set the attachments\n");
		}
		
	} else {
		pipelineInfo.renderPass = renderPass.value();
		graphicsResourceConfig->renderPass = renderPass.value();
		pipelineInfo.subpass = 0;
	}

	graphicsResourceConfig->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	graphicsResourceConfig->dynamicStateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	graphicsResourceConfig->dynamicStateInfo.pDynamicStates = graphicsResourceConfig->dynamicStates.data();
	graphicsResourceConfig->dynamicStateInfo.dynamicStateCount = graphicsResourceConfig->dynamicStates.size();

	pipelineInfo.pDynamicState = &graphicsResourceConfig->dynamicStateInfo;

	VkPipeline newPipeline;
	
	if (vkCreateGraphicsPipelines(device, PipelineManager::pipelineCache, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
		fmt::println("failed to create pipeline");
		return VK_NULL_HANDLE;

	}
	else {
		if (storeRes->type == PipelineType::Graphics) {
			storeRes->getGraphicsConfig()->shaderStages = graphicsResourceConfig->shaderStages;  // compiled shader stages (VkPipelineShaderStageCreateInfo)
			storeRes->getGraphicsConfig()->vertexInputInfo = graphicsResourceConfig->vertexInputInfo;
			storeRes->getGraphicsConfig()->inputAssembly = graphicsResourceConfig->inputAssembly;
			storeRes->getGraphicsConfig()->viewportStateInfo = graphicsResourceConfig->viewportStateInfo;
			storeRes->getGraphicsConfig()->rasterizer = graphicsResourceConfig->rasterizer;
			storeRes->getGraphicsConfig()->multisampling = graphicsResourceConfig->multisampling;
			storeRes->getGraphicsConfig()->colorBlendAttachment = graphicsResourceConfig->colorBlendAttachment;
			storeRes->getGraphicsConfig()->colorBlendingInfo = graphicsResourceConfig->colorBlendingInfo;
			storeRes->getGraphicsConfig()->colorBlendingInfo.pAttachments = &storeRes->getGraphicsConfig()->colorBlendAttachment;
			storeRes->getGraphicsConfig()->depthStencil = graphicsResourceConfig->depthStencil;
			storeRes->getGraphicsConfig()->dynamicStates = graphicsResourceConfig->dynamicStates;
			storeRes->getGraphicsConfig()->dynamicStateInfo = graphicsResourceConfig->dynamicStateInfo;
			storeRes->getGraphicsConfig()->dynamicStateInfo.pDynamicStates = storeRes->getGraphicsConfig()->dynamicStates.data();
			storeRes->pipelineLayout = res->pipelineLayout;
			storeRes->getGraphicsConfig()->renderInfo = graphicsResourceConfig->renderInfo;
			storeRes->getGraphicsConfig()->renderPass = graphicsResourceConfig->renderPass;
		
			storeRes->pipeline = res->pipeline;
		}
		return newPipeline;
	}
}

void PipelineBuilder::set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader) {
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->shaderStages.clear();

	graphicsResourceConfig->shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));

	graphicsResourceConfig->shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
}


void PipelineBuilder::set_input_topology(VkPrimitiveTopology topology) {
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->inputAssembly.topology = topology;
	// we are not going to use primitive restart on the entire tutorial so leave
	// it on false
	graphicsResourceConfig->inputAssembly.primitiveRestartEnable = VK_FALSE;
}

void PipelineBuilder::set_polygon_mode(VkPolygonMode mode) {
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->rasterizer.polygonMode = mode;
	graphicsResourceConfig->rasterizer.lineWidth = 1.f;
}

void PipelineBuilder::set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace) {
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->rasterizer.cullMode = cullMode;
	graphicsResourceConfig->rasterizer.frontFace = frontFace;
}

void PipelineBuilder::set_multisampling_none() {
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->multisampling.sampleShadingEnable = VK_FALSE;
	// multisampling defaulted to no multisampling (1 sample per pixel)
	graphicsResourceConfig->multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	graphicsResourceConfig->multisampling.minSampleShading = 1.0f;
	graphicsResourceConfig->multisampling.pSampleMask = nullptr;
	// no alpha to coverage either
	graphicsResourceConfig->multisampling.alphaToCoverageEnable = VK_FALSE;
	graphicsResourceConfig->multisampling.alphaToOneEnable = VK_FALSE;
}

void PipelineBuilder::disable_blending() {
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	// default write mask
	graphicsResourceConfig->colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	// no blending
	graphicsResourceConfig->colorBlendAttachment.blendEnable = VK_FALSE;
}

void PipelineBuilder::set_color_attachment_format(VkFormat format)
{
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->colorAttachmentformat = format;
	// connect the format to the renderInfo  structure
	graphicsResourceConfig->renderInfo.colorAttachmentCount = 1;
	graphicsResourceConfig->renderInfo.pColorAttachmentFormats = &graphicsResourceConfig->colorAttachmentformat;
}

void PipelineBuilder::set_depth_format(VkFormat format)
{
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->renderInfo.depthAttachmentFormat = format;
}

void PipelineBuilder::disable_depthtest()
{
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->depthStencil.depthTestEnable = VK_FALSE;
	graphicsResourceConfig->depthStencil.depthWriteEnable = VK_FALSE;
	graphicsResourceConfig->depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
	graphicsResourceConfig->depthStencil.depthBoundsTestEnable = VK_FALSE;
	graphicsResourceConfig->depthStencil.stencilTestEnable = VK_FALSE;
	graphicsResourceConfig->depthStencil.front = {};
	graphicsResourceConfig->depthStencil.back = {};
	graphicsResourceConfig->depthStencil.minDepthBounds = 0.f;
	graphicsResourceConfig->depthStencil.maxDepthBounds = 1.f;
}

void PipelineBuilder::enable_depthtest(bool depthWriteEnable, VkCompareOp op) {
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->depthStencil.depthTestEnable = VK_TRUE;
	graphicsResourceConfig->depthStencil.depthWriteEnable = depthWriteEnable;
	graphicsResourceConfig->depthStencil.depthCompareOp = op;
	graphicsResourceConfig->depthStencil.depthBoundsTestEnable = VK_FALSE;
	graphicsResourceConfig->depthStencil.stencilTestEnable = VK_FALSE;
	graphicsResourceConfig->depthStencil.front = {};
	graphicsResourceConfig->depthStencil.back = {};
	graphicsResourceConfig->depthStencil.minDepthBounds = 0.f;
	graphicsResourceConfig->depthStencil.maxDepthBounds = 1.f;
	
}

void PipelineBuilder::enable_blending_additive(){
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	graphicsResourceConfig->colorBlendAttachment.blendEnable = VK_TRUE;
	graphicsResourceConfig->colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	graphicsResourceConfig->colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	graphicsResourceConfig->colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	graphicsResourceConfig->colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	graphicsResourceConfig->colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	graphicsResourceConfig->colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

void PipelineBuilder::enable_blending_alphablend() {
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	graphicsResourceConfig->colorBlendAttachment.blendEnable = VK_TRUE;
	graphicsResourceConfig->colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	graphicsResourceConfig->colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	graphicsResourceConfig->colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	graphicsResourceConfig->colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	graphicsResourceConfig->colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	graphicsResourceConfig->colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}


