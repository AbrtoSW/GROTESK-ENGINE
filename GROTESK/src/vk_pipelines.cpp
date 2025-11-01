#include "vk_pipelines.h"
#include "vk_initializers.h"
#include "vk_engine.h"
#include <fstream>




void PipelineBuilder::clear() {

	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };

	graphicsResourceConfig->rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };

	graphicsResourceConfig->colorBlendAttachment = {};

	graphicsResourceConfig->multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };

	res->pLayout = {};

	graphicsResourceConfig->depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

	graphicsResourceConfig->renderInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };

	graphicsResourceConfig->shaderStages.clear();
}



VkPipeline PipelineBuilder::build_pipeline(VkDevice device, RenderMode mode, PipelineRes* storeResource)
{
	auto* cfg = res->getGraphicsConfig(); 

	cfg->viewportStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	cfg->viewportStateInfo.viewportCount = 1;
	cfg->viewportStateInfo.scissorCount = 1;

	cfg->colorBlendingInfo = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	cfg->colorBlendingInfo.logicOpEnable = VK_FALSE;
	cfg->colorBlendingInfo.logicOp = VK_LOGIC_OP_COPY;
	cfg->colorBlendingInfo.attachmentCount = 1; 
	cfg->colorBlendingInfo.pAttachments = &cfg->colorBlendAttachment; 

	cfg->vertexInputInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };


	cfg->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	cfg->dynamicStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	cfg->dynamicStateInfo.pDynamicStates = cfg->dynamicStates.data();
	cfg->dynamicStateInfo.dynamicStateCount = (uint32_t)cfg->dynamicStates.size();


	VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

	pipelineInfo.pStages = cfg->shaderStages.data();
	pipelineInfo.stageCount = (uint32_t)cfg->shaderStages.size();
	pipelineInfo.pVertexInputState = &cfg->vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &cfg->inputAssembly;
	pipelineInfo.pViewportState = &cfg->viewportStateInfo;
	pipelineInfo.pRasterizationState = &cfg->rasterizer;
	pipelineInfo.pMultisampleState = &cfg->multisampling;
	pipelineInfo.pColorBlendState = &cfg->colorBlendingInfo;
	pipelineInfo.pDepthStencilState = &cfg->depthStencil;
	pipelineInfo.pDynamicState = &cfg->dynamicStateInfo;
	pipelineInfo.layout = res->pLayout;

	cfg->renderMode = mode;
	if (cfg->renderMode == RenderMode::Dynamic) {
		pipelineInfo.renderPass = VK_NULL_HANDLE;
		pipelineInfo.subpass = 0;
		cfg->renderPass = VK_NULL_HANDLE;
		pipelineInfo.pNext = &cfg->renderInfo;  

		const bool hasColors = (cfg->renderInfo.colorAttachmentCount > 0) &&
			(cfg->renderInfo.pColorAttachmentFormats != nullptr);
		const bool colorsValid = hasColors; 
		const bool depthOk = (cfg->renderInfo.depthAttachmentFormat != VK_FORMAT_UNDEFINED) || true;

		if (!colorsValid) {
			//fmt::print("PipelineBuilder: dynamic rendering needs colorAttachmentCount>0 and pColorAttachmentFormats set\n");
		}
	}
	else {
		pipelineInfo.renderPass = cfg->renderPass;   
		pipelineInfo.subpass = 0;
		pipelineInfo.pNext = nullptr; 
	}

	VkPipeline newPipeline = VK_NULL_HANDLE;
	if (vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
		fmt::println("failed to create pipeline");
		return VK_NULL_HANDLE;
	}

	res->pipeline = newPipeline;

	if (storeResource) {
		if (storeResource->type == Uninitialized) {
			//fmt::print("pipeline resource .type is not initialized correctly, hotloading won't work\n");
		}
		else if (storeResource->type == PipelineType::Graphics) {
			auto* dst = storeResource->getGraphicsConfig();
			*dst = *cfg;

			dst->colorBlendingInfo.pAttachments = &dst->colorBlendAttachment;
			dst->dynamicStateInfo.pDynamicStates = dst->dynamicStates.data();

			storeResource->pLayout = res->pLayout;
			storeResource->pipeline = newPipeline;
		}
	}

	return newPipeline;
}

void PipelineBuilder::set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader) {
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->shaderStages.clear();

	graphicsResourceConfig->shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));

	graphicsResourceConfig->shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
}

void PipelineBuilder::set_renderpass(VkRenderPass renderpass) {
	auto* graphicsResourceConfig = res->getGraphicsConfig();

	graphicsResourceConfig->renderPass = renderpass;
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



PID NewPipelineManager::registerPipeline(PipelineRes& pRes, Hotloadable hotload, std::optional<const char*> name, bool autoDestruction) {
	PID id = nextID();

	if (name)
		pRes.name = *name;

	auto [it, inserted] = pipelineStorage.emplace(id, std::move(pRes));
	auto& stored = it->second;

	if (autoDestruction) {
		if (stored.pipeline != VK_NULL_HANDLE && queuedPipelines.insert(stored.pipeline).second)
			VulkanEngine::Get().mainDeletionQueue.push_pipeline(stored.pipeline);

		if (stored.pLayout != VK_NULL_HANDLE && queuedLayouts.insert(stored.pLayout).second)
			VulkanEngine::Get().mainDeletionQueue.push_pipeline_layout(stored.pLayout);
	}

	if (hotload == Hotloadable::True) {
		track_shaders_for_hotload(stored);
	}

	return id;
}

void NewPipelineManager::showInfo() {
	for (const auto& pair : pipelineStorage) {
		PID id = pair.first;
		const auto& info = pair.second;
		fmt::print("ID = {}\n  Pipeline = {:p}  Layout = {:p}\n name = {}\n",
			id,
			(const void*)info.pipeline,
			(const void*)info.pLayout,
			(info.name ? info.name : "didn't assign name"));
	}
}


void NewPipelineManager::track_shaders_for_hotload(PipelineRes& resource) {
	fmt::print("Before link_shader: vertex='{}', fragment='{}'\n",
		resource.shaderType.vertexShader.file.path,
		resource.shaderType.fragmentShader.file.path);

	auto add = [&](const std::string& path) {
		if (path.empty()) return;
		auto& vec = shaderMap[path];
		if (std::find(vec.begin(), vec.end(), &resource) == vec.end()) {
			vec.push_back(&resource);
		}
		};

	if (!resource.shaderType.vertexShader.file.path.empty()) {
		add(resource.shaderType.vertexShader.file.path);
		fmt::print("Registered vertex shader: {} lastModified: {}\n",
			resource.shaderType.vertexShader.file.path,
			resource.shaderType.vertexShader.lastModified.time_since_epoch().count());
	}

	if (!resource.shaderType.fragmentShader.file.path.empty()) {
		add(resource.shaderType.fragmentShader.file.path);
		fmt::print("Registered fragment shader: {} lastModified: {}\n",
			resource.shaderType.fragmentShader.file.path,
			resource.shaderType.fragmentShader.lastModified.time_since_epoch().count());
	}

	if (!resource.shaderType.geometryShader.file.path.empty()) {
		add(resource.shaderType.geometryShader.file.path);
		fmt::print("Registered geometry shader: {}\n",
			resource.shaderType.geometryShader.file.path);
	}

	if (!resource.shaderType.computeShader.file.path.empty()) {
		add(resource.shaderType.computeShader.file.path);
		fmt::print("Registered compute shader: {}\n",
			resource.shaderType.computeShader.file.path);
	}
}




VkPipeline NewPipelineManager::getPipeline(PID id) const {
	auto it = pipelineStorage.find(id);
	return (it != pipelineStorage.end()) ? it->second.pipeline : VK_NULL_HANDLE;
}
VkPipelineLayout NewPipelineManager::getLayout(PID id) const {
	auto it = pipelineStorage.find(id);
	return (it != pipelineStorage.end()) ? it->second.pLayout : VK_NULL_HANDLE;
}