#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "images.hpp"

struct Application;

VkRenderPass createGeometryRenderPass(const Application& app, const std::vector<Image>& attachments);
VkRenderPass createLightingRenderPass(const Application& app);
