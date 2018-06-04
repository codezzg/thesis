#pragma once

#include "images.hpp"
#include <vector>
#include <vulkan/vulkan.h>

struct Application;

VkRenderPass createForwardRenderPass(const Application& app);
VkRenderPass createMultipassRenderPass(const Application& app);
