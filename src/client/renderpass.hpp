#pragma once

#include "images.hpp"
#include <vector>
#include <vulkan/vulkan.h>

struct Application;

VkRenderPass createMultipassRenderPass(const Application& app);
