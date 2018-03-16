#pragma once

#include <vulkan/vulkan.h>
#include "application.hpp"

uint32_t findMemoryType(const Application& app, uint32_t typeFilter, VkMemoryPropertyFlags properties);

void createBuffer(
		const Application& app,
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties,
		/* out */ VkBuffer& buffer,
		/* out */ VkDeviceMemory& bufferMemory);

void copyBuffer(const Application& app, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
void copyBufferToImage(const Application& app, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
