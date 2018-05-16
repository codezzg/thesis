#include "buffers.hpp"
#include <array>
#include <cstring>
#include <unordered_set>
#include "commands.hpp"
#include "phys_device.hpp"
#include "application.hpp"
#include "vertex.hpp"
#include "vulk_errors.hpp"
#include "logging.hpp"

using namespace logging;

void BufferAllocator::addBuffer(
		Buffer& buffer,
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	createInfos.emplace_back(bufferInfo);
	this->properties.emplace_back(properties);

	buffer.size = size;
	buffers.emplace_back(&buffer);
}

void BufferAllocator::create(const Application& app) {
	// (memory type) => (memory size)
	std::unordered_map<uint32_t, VkDeviceSize> requiredSizes;

	struct BufMemInfo {
		uint32_t memTypeNeeded;
	};

	std::vector<BufMemInfo> bufInfos;
	bufInfos.reserve(createInfos.size());

	// Create the buffers and figure out what memory they need
	for (unsigned i = 0; i < createInfos.size(); ++i) {
		BufMemInfo bufMemInfo = {};

		VkBuffer bufHandle;
		VLKCHECK(vkCreateBuffer(app.device, &createInfos[i], nullptr, &bufHandle));
		app.validation.addObjectInfo(bufHandle, __FILE__, __LINE__);
		buffers[i]->handle = bufHandle;

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(app.device, bufHandle, &memRequirements);

		const auto memType = findMemoryType(app.physicalDevice,
				memRequirements.memoryTypeBits, properties[i]);
		bufMemInfo.memTypeNeeded = memType;
		buffers[i]->offset = requiredSizes[memType];
		requiredSizes[memType] += memRequirements.size;

		bufInfos.emplace_back(bufMemInfo);
	}

	// The newly allocated device memories
	std::unordered_map<uint32_t, VkDeviceMemory> memories;
	memories.reserve(requiredSizes.size());

	// Allocate memory of the proper types
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	for (const auto& pair : requiredSizes) {
		allocInfo.memoryTypeIndex = pair.first;
		allocInfo.allocationSize = pair.second;
		VkDeviceMemory bufferMemory;
		VLKCHECK(vkAllocateMemory(app.device, &allocInfo, nullptr, &bufferMemory));
		app.validation.addObjectInfo(bufferMemory, __FILE__, __LINE__);

		memories[pair.first] = bufferMemory;
	}

	// Bind the memory to the buffers
	for (unsigned i = 0; i < buffers.size(); ++i) {
		auto& info = bufInfos[i];
		auto buf = buffers[i];
		VLKCHECK(vkBindBufferMemory(app.device, buf->handle, memories[info.memTypeNeeded], buf->offset));
		buf->memory = memories[info.memTypeNeeded];
	}

	info("Created ", buffers.size(), " buffers via ", memories.size(), " allocations.");
}

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
	allocInfo.memoryTypeIndex = findMemoryType(app.physicalDevice, memRequirements.memoryTypeBits, properties);
	allocInfo.allocationSize = memRequirements.size;

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
	// position, normal, texCoords
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

void destroyAllBuffers(VkDevice device, const std::vector<Buffer>& buffers) {
	std::unordered_set<VkDeviceMemory> mems;
	for (auto& b : buffers) {
		mems.emplace(b.memory);
		vkDestroyBuffer(device, b.handle, nullptr);
	}

	for (auto& mem : mems)
		vkFreeMemory(device, mem, nullptr);
}
