#pragma once

#include <vulkan/vulkan.h>

struct Application;

VkRenderPass createRenderPass(const Application& app);
