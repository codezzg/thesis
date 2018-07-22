#pragma once

#include <vulkan/vulkan.h>

struct Application;

VkRenderPass createMultipassRenderPass(const Application& app);
