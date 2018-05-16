#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>

/** A MemoryBlock defines a region within a device memory allocation */
struct MemoryBlock final {
	VkDeviceMemory memory;
	VkDeviceSize offset;
	VkDeviceSize size;
	int32_t _idx = -1; // if < 0, block is free
	uint32_t memType;
};

/** A MemoryChunk represents a memory region created by a single allocation. */
struct MemoryChunk final {
	VkDevice device;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize size;
	uint32_t memoryTypeIndex;

	std::vector<MemoryBlock> blocks;

	bool alloc(VkDeviceSize size, VkDeviceSize align, MemoryBlock& block);
	void dealloc(MemoryBlock& block);

private:
	friend MemoryChunk createMemoryChunk(VkDevice, VkDeviceSize, uint32_t);

	uint32_t blockIdx = 0;
};

MemoryChunk createMemoryChunk(VkDevice device, VkDeviceSize size, uint32_t memoryTypeIndex);

class VulkanAllocator final {
	std::unordered_map<uint32_t, MemoryChunk> chunks;

	VkDevice device;

public:
	explicit VulkanAllocator(VkDevice device) : device{ device } {}

	MemoryBlock alloc(uint32_t type, VkDeviceSize size, VkDeviceSize align);
	void dealloc(MemoryBlock& block);
};
