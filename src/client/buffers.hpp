#pragma once

#include <vulkan/vulkan.h>
#include "application.hpp"

struct Buffer final {
	VkBuffer handle;
	VkDeviceMemory memory;
};

Buffer createBuffer(
		const Application& app,
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties);

void copyBuffer(const Application& app, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
void copyBufferToImage(const Application& app, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
