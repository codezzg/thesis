#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include "application.hpp"

VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);

VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

VkExtent2D chooseSwapExtent(const Application& app, const VkSurfaceCapabilitiesKHR& capabilities);
