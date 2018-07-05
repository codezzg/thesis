#pragma once

#include <vulkan/vulkan.h>

struct Application;

namespace shared {
struct SpirvShader;
}

/** Creates a ShaderModule from SPIR-V shader file `fname` */
VkShaderModule createShaderModule(const Application& app, const char* fname);

/** Creates a ShaderModule from struct `shader` */
VkShaderModule createShaderModule(const Application& app, const shared::SpirvShader& shader);
