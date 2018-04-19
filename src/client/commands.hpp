#pragma once

#include <vulkan/vulkan.h>

struct Application;

VkCommandPool createCommandPool(const Application& app);

VkCommandBuffer beginSingleTimeCommands(const Application& app, VkCommandPool commandPool);

void endSingleTimeCommands(VkDevice device, VkQueue graphicsQueue,
		VkCommandPool commandPool, VkCommandBuffer commandBuffer);
