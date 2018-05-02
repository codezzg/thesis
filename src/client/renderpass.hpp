#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "images.hpp"

struct Application;

VkRenderPass createGeometryRenderPass(const Application& app);
VkRenderPass createLightingRenderPass(const Application& app);
