#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

struct Application;

struct Buffer final {
	VkBuffer handle;
	VkDeviceMemory memory;
	VkDeviceSize size;

	void destroy(VkDevice device) {
		vkDestroyBuffer(device, handle, nullptr);
		vkFreeMemory(device, memory, nullptr);
	}
};

struct MVPUniformBufferObject final {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

struct CompositionUniformBufferObject final {
	glm::vec4 viewPos;
};

Buffer createBuffer(
		const Application& app,
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties);

void copyBuffer(const Application& app, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
void copyBufferToImage(const Application& app, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
Buffer createScreenQuadVertexBuffer(const Application& app);