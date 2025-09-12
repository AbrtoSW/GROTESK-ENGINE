#include "util.h"

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



std::filesystem::file_time_type getFileTimeStamp(const std::string& shaderFile) {
	return std::filesystem::last_write_time(shaderFile);
}