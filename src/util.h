#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <filesystem>

extern TBuiltInResource DefaultTBuiltInResource;

std::string readFile(const std::string& filepath);

std::filesystem::file_time_type getFileTimeStamp(const std::string& shaderFile);