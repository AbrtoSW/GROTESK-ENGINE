#include "vk_pipelines.h"
#include "vk_initializers.h"
#include <fstream>


//see if you can just forward declare the renderer so its not the whole class 
#include "vk_renderer.h"


bool vkutil::load_shader_module(const char* filePath,VkDevice device, VkShaderModule* outShaderModule) {
	// open the file. With cursor at the end
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		return false;
	}

	// find what the size of the file is by looking up the location of the cursor
	// because the cursor is at the end, it gives the size directly in bytes
	size_t fileSize = (size_t)file.tellg();

	// spirv expects the buffer to be on uint32, so make sure to reserve a int
	// vector big enough for the entire file
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	// put file cursor at beginning
	file.seekg(0);

	// load the entire file into the buffer
	file.read((char*)buffer.data(), fileSize);

	// now that the file is loaded into the buffer, we can close it
	file.close();

	// create a new shader module, using the buffer we loaded
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	// codeSize has to be in bytes, so multply the ints in the buffer by size of
	// int to know the real size of the buffer
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	// check that the creation goes well.
	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		return false;
	}
	*outShaderModule = shaderModule;

	return true;
}


VkShaderModule vkutil::compileToSPV(VkDevice device, const std::string& shaderFile, EShLanguage stage) {

	std::string source = readFile(shaderFile);
	const char* sourcePtr = source.c_str();

	//apparently initliaze process should be at startup of vulkan
	
	EShMessages messages = (EShMessages)(EShMsgDefault | EShMsgVulkanRules | EShMsgSpvRules);

	glslang::TShader shader(stage);
	shader.setStrings(&sourcePtr, 1);
	if (!shader.parse(&DefaultTBuiltInResource, 110, false, messages)) {
		fmt::print("Shader compile error: {}\n", shader.getInfoLog());
		throw std::runtime_error(shader.getInfoLog());
	}

	glslang::TProgram program;
	program.addShader(&shader);
	if (!program.link(messages)) {
		throw std::runtime_error(program.getInfoLog());
	}

	std::vector<uint32_t> spirv;
	GlslangToSpv(*program.getIntermediate(stage), spirv);
	
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = spirv.size() * sizeof(uint32_t);
	createInfo.pCode = spirv.data();

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create shader module");
	}

	return shaderModule;
}

void PipelineBuilder::clear() {
	// clear all of the structs we need back to 0 with their correct stype

	res.inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };

	res.rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };

	res.colorBlendAttachment = {};

	res.multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };

	res.pipelineLayout = {};

	res.depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

	res.renderInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };

	res.shaderStages.clear();
}



VkPipeline PipelineBuilder::build_pipeline(VkDevice device, RenderMode mode, const std::optional<VkRenderPass>& renderPass, PipelineManager::PipelineResources* storeRes) {


	res.viewportStateInfo = {};
	res.viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	res.viewportStateInfo.pNext = nullptr;

	res.viewportStateInfo.viewportCount = 1;
	res.viewportStateInfo.scissorCount = 1;


	res.colorBlendingInfo = {};
	res.colorBlendingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	res.colorBlendingInfo.pNext = nullptr;

	res.colorBlendingInfo.logicOpEnable = VK_FALSE;
	res.colorBlendingInfo.logicOp = VK_LOGIC_OP_COPY;
	res.colorBlendingInfo.attachmentCount = 1;
	res.colorBlendingInfo.pAttachments = &res.colorBlendAttachment;

	 res.vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

	VkGraphicsPipelineCreateInfo pipelineInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.pNext = &res.renderInfo;

	pipelineInfo.stageCount = (uint32_t)res.shaderStages.size();
	pipelineInfo.pStages = res.shaderStages.data();
	pipelineInfo.pVertexInputState = &res.vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &res.inputAssembly;
	pipelineInfo.pViewportState = &res.viewportStateInfo;
	pipelineInfo.pRasterizationState = &res.rasterizer;
	pipelineInfo.pMultisampleState = &res.multisampling;
	pipelineInfo.pColorBlendState = &res.colorBlendingInfo;
	pipelineInfo.pDepthStencilState = &res.depthStencil;
	pipelineInfo.layout = res.pipelineLayout;

	res.renderMode = mode;
	
	if (res.renderMode == RenderMode::Dynamic) {
		pipelineInfo.renderPass = VK_NULL_HANDLE;
		pipelineInfo.subpass = 0;
		res.renderPass = VK_NULL_HANDLE;

		//later implement a more dynamic way to attach formats if for example you need to use only color formats or depth formats using the dynamic render option
		//it could be a std::optional so the pipline builder doesnt need so many parameters if not neccessary
		//make sure this works correctly, reminder once you want to add dynamic rendering to sync with classic renderpass
		//make sure you figure out how to pass depth information correctly from dynamic render pass -> to classic render pass
		if (res.renderInfo.pColorAttachmentFormats || res.renderInfo.depthAttachmentFormat == VK_FORMAT_UNDEFINED) {
			fmt::print("must set the attachments\n");
		}
		
	} else {
		pipelineInfo.renderPass = renderPass.value();
		res.renderPass = renderPass.value();
		pipelineInfo.subpass = 0;
	}

	res.dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	res.dynamicStateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	res.dynamicStateInfo.pDynamicStates = res.dynamicStates.data();
	res.dynamicStateInfo.dynamicStateCount = res.dynamicStates.size();

	pipelineInfo.pDynamicState = &res.dynamicStateInfo;

	VkPipeline newPipeline;
	
	if (vkCreateGraphicsPipelines(device, PipelineManager::pipelineCache, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
		fmt::println("failed to create pipeline");
		return VK_NULL_HANDLE;

	}
	else {
		if (storeRes) {
			storeRes->shaderStages = res.shaderStages;  // compiled shader stages (VkPipelineShaderStageCreateInfo)
			storeRes->vertexInputInfo = res.vertexInputInfo;
			storeRes->inputAssembly = res.inputAssembly;
			storeRes->viewportStateInfo = res.viewportStateInfo;
			storeRes->rasterizer = res.rasterizer;
			storeRes->multisampling = res.multisampling;
			storeRes->colorBlendAttachment = res.colorBlendAttachment;
			storeRes->colorBlendingInfo = res.colorBlendingInfo;
			storeRes->colorBlendingInfo.pAttachments = &storeRes->colorBlendAttachment;
			storeRes->depthStencil = res.depthStencil;
			storeRes->dynamicStates = res.dynamicStates;
			storeRes->dynamicStateInfo = res.dynamicStateInfo;
			storeRes->dynamicStateInfo.pDynamicStates = storeRes->dynamicStates.data();
			storeRes->pipelineLayout = res.pipelineLayout;
			storeRes->renderInfo = res.renderInfo;
			storeRes->renderPass = res.renderPass;
		
			storeRes->pipeline = res.pipeline;
		}
		return newPipeline;
	}
}

void PipelineBuilder::set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader)
{
	res.shaderStages.clear();

	res.shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));

	res.shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
}


void PipelineBuilder::set_input_topology(VkPrimitiveTopology topology)
{
	res.inputAssembly.topology = topology;
	// we are not going to use primitive restart on the entire tutorial so leave
	// it on false
	res.inputAssembly.primitiveRestartEnable = VK_FALSE;
}

void PipelineBuilder::set_polygon_mode(VkPolygonMode mode)
{
	res.rasterizer.polygonMode = mode;
	res.rasterizer.lineWidth = 1.f;
}

void PipelineBuilder::set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace)
{
	res.rasterizer.cullMode = cullMode;
	res.rasterizer.frontFace = frontFace;
}

void PipelineBuilder::set_multisampling_none()
{
	res.multisampling.sampleShadingEnable = VK_FALSE;
	// multisampling defaulted to no multisampling (1 sample per pixel)
	res.multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	res.multisampling.minSampleShading = 1.0f;
	res.multisampling.pSampleMask = nullptr;
	// no alpha to coverage either
	res.multisampling.alphaToCoverageEnable = VK_FALSE;
	res.multisampling.alphaToOneEnable = VK_FALSE;
}

void PipelineBuilder::disable_blending()
{
	// default write mask
	res.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	// no blending
	res.colorBlendAttachment.blendEnable = VK_FALSE;
}

void PipelineBuilder::set_color_attachment_format(VkFormat format)
{
	
	res.colorAttachmentformat = format;
	// connect the format to the renderInfo  structure
	res.renderInfo.colorAttachmentCount = 1;
	res.renderInfo.pColorAttachmentFormats = &res.colorAttachmentformat;
}

void PipelineBuilder::set_depth_format(VkFormat format)
{
	res.renderInfo.depthAttachmentFormat = format;
}

void PipelineBuilder::disable_depthtest()
{
	res.depthStencil.depthTestEnable = VK_FALSE;
	res.depthStencil.depthWriteEnable = VK_FALSE;
	res.depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
	res.depthStencil.depthBoundsTestEnable = VK_FALSE;
	res.depthStencil.stencilTestEnable = VK_FALSE;
	res.depthStencil.front = {};
	res.depthStencil.back = {};
	res.depthStencil.minDepthBounds = 0.f;
	res.depthStencil.maxDepthBounds = 1.f;
}

void PipelineBuilder::enable_depthtest(bool depthWriteEnable, VkCompareOp op) {
	res.depthStencil.depthTestEnable = VK_TRUE;
	res.depthStencil.depthWriteEnable = depthWriteEnable;
	res.depthStencil.depthCompareOp = op;
	res.depthStencil.depthBoundsTestEnable = VK_FALSE;
	res.depthStencil.stencilTestEnable = VK_FALSE;
	res.depthStencil.front = {};
	res.depthStencil.back = {};
	res.depthStencil.minDepthBounds = 0.f;
	res.depthStencil.maxDepthBounds = 1.f;
	
}

void PipelineBuilder::enable_blending_additive(){
 
	res.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	res.colorBlendAttachment.blendEnable = VK_TRUE;
	res.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	res.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	res.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	res.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	res.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	res.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

void PipelineBuilder::enable_blending_alphablend() {
	res.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	res.colorBlendAttachment.blendEnable = VK_TRUE;
	res.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	res.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	res.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	res.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	res.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	res.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}


