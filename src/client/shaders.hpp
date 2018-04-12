#pragma once

#include <vulkan/vulkan.h>

struct Application;

VkShaderModule createShaderModule(const Application& app, const char *fname);
