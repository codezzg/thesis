#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "images.hpp"

struct Application;

VkRenderPass createForwardRenderPass(const Application& app);
VkRenderPass createMultipassRenderPass(const Application& app);
