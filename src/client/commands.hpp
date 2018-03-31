#pragma once

#include <vulkan/vulkan.h>

VkCommandPool createCommandPool(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);

VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);

void endSingleTimeCommands(VkDevice device, VkQueue graphicsQueue,
		VkCommandPool commandPool, VkCommandBuffer commandBuffer);
