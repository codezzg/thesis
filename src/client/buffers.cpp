#include "buffers.hpp"
#include <array>
#include <cstring>
#include "commands.hpp"
#include "phys_device.hpp"
#include "application.hpp"
#include "vertex.hpp"
#include "vulk_errors.hpp"

Buffer createBuffer(
		const Application& app,
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer bufferHandle;
	VLKCHECK(vkCreateBuffer(app.device, &bufferInfo, nullptr, &bufferHandle));
	app.validation.addObjectInfo(bufferHandle, __FILE__, __LINE__);

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(app.device, bufferHandle, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(app.physicalDevice, memRequirements.memoryTypeBits, properties);

	VkDeviceMemory bufferMemory;
	VLKCHECK(vkAllocateMemory(app.device, &allocInfo, nullptr, &bufferMemory));
	app.validation.addObjectInfo(bufferMemory, __FILE__, __LINE__);

	VLKCHECK(vkBindBufferMemory(app.device, bufferHandle, bufferMemory, 0));

	Buffer buffer;
	buffer.handle = bufferHandle;
	buffer.memory = bufferMemory;
	buffer.size = size;

	return buffer;
}

void copyBuffer(const Application& app, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
	auto commandBuffer = beginSingleTimeCommands(app, app.commandPool);

	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = 0;
	copyRegion.dstOffset = 0;
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	endSingleTimeCommands(app.device, app.queues.graphics, app.commandPool, commandBuffer);
}

void copyBufferToImage(const Application& app, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(app, app.commandPool);

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = { width, height, 1 };

	vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	endSingleTimeCommands(app.device, app.queues.graphics, app.commandPool, commandBuffer);
}

static const std::array<Vertex, 4> quadVertices = {
	Vertex{ glm::vec3{ -1.0f,  1.0f, 0.0f }, glm::vec3{}, glm::vec2{ 0.0f, 1.0f } },
	Vertex{ glm::vec3{ -1.0f, -1.0f, 0.0f }, glm::vec3{}, glm::vec2{ 0.0f, 0.0f } },
	Vertex{ glm::vec3{  1.0f,  1.0f, 0.0f }, glm::vec3{}, glm::vec2{ 1.0f, 1.0f } },
	Vertex{ glm::vec3{  1.0f, -1.0f, 0.0f }, glm::vec3{}, glm::vec2{ 1.0f, 0.0f } },
};

Buffer createScreenQuadVertexBuffer(const Application& app) {
	auto stagingBuffer = createBuffer(app, sizeof(Vertex) * quadVertices.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	void* data;
	VLKCHECK(vkMapMemory(app.device, stagingBuffer.memory, 0, stagingBuffer.size, 0, &data));
	memcpy(data, quadVertices.data(), stagingBuffer.size);
	vkUnmapMemory(app.device, stagingBuffer.memory);

	auto buffer = createBuffer(app, sizeof(Vertex) * quadVertices.size(),
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	copyBuffer(app, stagingBuffer.handle, buffer.handle, buffer.size);

	stagingBuffer.destroy(app.device);

	return buffer;
}
