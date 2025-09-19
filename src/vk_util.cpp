#include "vk_util.h"

std::string readFile(const std::string& filepath) {
	std::ifstream file(filepath, std::ios::in | std::ios::binary);
	if (!file.is_open()) {
		throw std::runtime_error("Failed to open file: " + filepath);
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	file.close();
	return buffer.str();
}

TBuiltInResource DefaultTBuiltInResource = {
	32,    // maxLights
	6,     // maxClipPlanes
	32,    // maxTextureUnits
	32,    // maxTextureCoords
	64,    // maxVertexAttribs
	4096,  // maxVertexUniformComponents
	64,    // maxVaryingFloats
	32,    // maxVertexTextureImageUnits
	80,    // maxCombinedTextureImageUnits
	32,    // maxTextureImageUnits
	4096,  // maxFragmentUniformComponents
	32,    // maxDrawBuffers
	128,   // maxVertexUniformVectors
	8,     // maxVaryingVectors
	16,    // maxFragmentUniformVectors
	16,    // maxVertexOutputVectors
	15,    // maxFragmentInputVectors
	-8,    // minProgramTexelOffset
	7,     // maxProgramTexelOffset
	8,     // maxClipDistances
	65535, // maxComputeWorkGroupCountX
	65535, // maxComputeWorkGroupCountY
	65535, // maxComputeWorkGroupCountZ
	1024,  // maxComputeWorkGroupSizeX
	1024,  // maxComputeWorkGroupSizeY
	64,    // maxComputeWorkGroupSizeZ
	1024,  // maxComputeUniformComponents
	16,    // maxComputeTextureImageUnits
	8,     // maxComputeImageUniforms
	8,     // maxComputeAtomicCounters
	1,     // maxComputeAtomicCounterBuffers
	60,    // maxVaryingComponents
	64,    // maxVertexOutputComponents
	64,    // maxGeometryInputComponents
	128,   // maxGeometryOutputComponents
	64,    // maxFragmentInputComponents
	16,    // maxImageUnits
	8,     // maxCombinedImageUnitsAndFragmentOutputs
	8,     // maxCombinedShaderOutputResources
	0,     // maxImageSamples
	8,     // maxVertexImageUniforms
	8,     // maxTessControlImageUniforms
	8,     // maxTessEvaluationImageUniforms
	8,     // maxGeometryImageUniforms
	8,     // maxFragmentImageUniforms
	8,     // maxCombinedImageUniforms
	16,    // maxGeometryTextureImageUnits
	256,   // maxGeometryOutputVertices
	1024,  // maxGeometryTotalOutputComponents
	1024,  // maxGeometryUniformComponents
	64,    // maxGeometryVaryingComponents
	128,   // maxTessControlInputComponents
	128,   // maxTessControlOutputComponents
	16,    // maxTessControlTextureImageUnits
	1024,  // maxTessControlUniformComponents
	4096,  // maxTessControlTotalOutputComponents
	128,   // maxTessEvaluationInputComponents
	128,   // maxTessEvaluationOutputComponents
	16,    // maxTessEvaluationTextureImageUnits
	1024,  // maxTessEvaluationUniformComponents
	120,   // maxTessPatchComponents
	32,    // maxPatchVertices
	32,    // maxTessGenLevel
	16,    // maxViewports
	8,     // maxVertexAtomicCounters
	8,     // maxTessControlAtomicCounters
	8,     // maxTessEvaluationAtomicCounters
	8,     // maxGeometryAtomicCounters
	8,     // maxFragmentAtomicCounters
	8,     // maxCombinedAtomicCounters
	1,     // maxAtomicCounterBindings
	1,     // maxVertexAtomicCounterBuffers
	1,     // maxTessControlAtomicCounterBuffers
	1,     // maxTessEvaluationAtomicCounterBuffers
	1,     // maxGeometryAtomicCounterBuffers
	1,     // maxFragmentAtomicCounterBuffers
	1,     // maxCombinedAtomicCounterBuffers
	16384, // maxAtomicCounterBufferSize
	4,     // maxTransformFeedbackBuffers
	64,    // maxTransformFeedbackInterleavedComponents
	8,     // maxCullDistances
	8,     // maxCombinedClipAndCullDistances
	4,     // maxSamples
	256,   // maxMeshOutputVerticesNV
	512,   // maxMeshOutputPrimitivesNV
	1024, 1024, 64, // maxMeshWorkGroupSizeX/Y/Z_NV
	1024, 1024, 64, // maxTaskWorkGroupSizeX/Y/Z_NV
	4,     // maxMeshViewCountNV
	256,   // maxMeshOutputVerticesEXT
	512,   // maxMeshOutputPrimitivesEXT
	1024, 1024, 64, // maxMeshWorkGroupSizeX/Y/Z_EXT
	1024, 1024, 64, // maxTaskWorkGroupSizeX/Y/Z_EXT
	4,     // maxMeshViewCountEXT
	1,     // maxDualSourceDrawBuffersEXT

	// Limits
	{ true, true, true, true, true, true, true, true, true } // nonInductiveForLoops, whileLoops, doWhileLoops, generalUniformIndexing, etc.
};



std::filesystem::file_time_type shaderUtil::getFileTimeStamp(const std::string& shaderFile) {
	return std::filesystem::last_write_time(shaderFile);
}



bool shaderUtil::load_shader_module(const char* filePath, VkDevice device, VkShaderModule* outShaderModule) {
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		return false;
	}

	size_t fileSize = (size_t)file.tellg();

	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);

	file.read((char*)buffer.data(), fileSize);

	file.close();

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

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

glslang::TShader::Includer::IncludeResult* RuntimeIncluder::includeLocal(const char* headerName, const char* includerName, size_t includeDepth) {

	std::ifstream file("C:/Users/Alberto/source/repos/GROTESK/GROTESK/res/shaders/" + std::string(headerName));
	if (!file.is_open()) {
    fmt::print("Failed to open include file: {}\n", headerName);
    return nullptr;
}

	std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	// Allocate heap memory for glslang
	char* buffer = new char[contents.size() + 1];
	memcpy(buffer, contents.c_str(), contents.size() + 1);

	return new IncludeResult(headerName, buffer, contents.size(), nullptr);
}

glslang::TShader::Includer::IncludeResult* RuntimeIncluder::includeSystem(const char* headerName,const char* includerName, size_t includeDepth) {
	return includeLocal(headerName, includerName, includeDepth);
}


void RuntimeIncluder::releaseInclude(IncludeResult* result)
{
	if (!result) return;
	delete[] result->headerData;
	delete result;
}

VkShaderModule shaderUtil::compileToSPV(VkDevice device, const std::string& shaderFile, EShLanguage stage) {

	std::string source = readFile(shaderFile);
	const char* sourcePtr = source.c_str();


	EShMessages messages = (EShMessages)(EShMsgDefault | EShMsgVulkanRules | EShMsgSpvRules);

	glslang::TShader shader(stage);
	shader.setStrings(&sourcePtr, 1);
	

	RuntimeIncluder includer;

	if (!shader.parse(&DefaultTBuiltInResource, 110, false, messages, includer)) {
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

	// you can make a cache to make compilation times quicker 

	return shaderModule;
}



void DeletionQueue::push_deletion_lambda(std::function<void()>&& function) {
	deletors.push_back(function);
}

void DeletionQueue::flush_deletion_lambda() {
	for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
		(*it)();
	}
	deletors.clear();
}

void DeletionQueue::flushFrameResources(VmaAllocator& vmaAllocator) {
	for (auto& b : vmaAllocatedBuffer) {
		if (b.buffer != VK_NULL_HANDLE && b.allocation != VK_NULL_HANDLE) {
			vmaDestroyBuffer(vmaAllocator, b.buffer, b.allocation);
			b.buffer = VK_NULL_HANDLE;
			b.allocation = VK_NULL_HANDLE;
		}
	}
	vmaAllocatedBuffer.clear(); // clear after destruction to prevent double frees
}

void DeletionQueue::flushMainResources(VkDevice device, VmaAllocator& vmaAllocator) {


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

void DeletionQueue::resizeFlush(VkDevice device, VmaAllocator& vmaAllocator) {

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

