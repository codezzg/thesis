#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include "logging.hpp"

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

#ifndef NDEBUG
class MemoryMonitor final {
	std::unordered_map<VkDeviceMemory, VkMemoryAllocateInfo> allocInfo;
	uint32_t nAllocs = 0;
	uint32_t nFrees = 0;
	VkDeviceSize totSize = 0;

public:
	void newAlloc(VkDeviceMemory memory, const VkMemoryAllocateInfo& info) {
		++nAllocs;
		totSize += info.allocationSize;
		allocInfo[memory] = info;
		logging::info("--> New alloc type: ", info.memoryTypeIndex, ", size: ", info.allocationSize, " B (",
			info.allocationSize / 1024 / 1024, " MiB)");
		report();
	}

	void newFree(VkDeviceMemory memory) {
		++nFrees;
		const auto& info = allocInfo[memory];
		totSize -= info.allocationSize;
		logging::info("<-- new free type: ", info.memoryTypeIndex, ", size: ", info.allocationSize, " B (",
			info.allocationSize / 1024 / 1024, " MiB)");
		allocInfo.erase(memory);
		report();
	}

	void report() {
		logging::log(LOGLV_INFO, true, "--------------------------");
		logging::log(LOGLV_INFO, true, "# allocations so far: ", nAllocs, 
			"\n# frees so far: ", nFrees,
			"\nTotal device mem used: ", totSize, " B (", totSize / 1024 / 1024, " MiB)");

		std::unordered_map<uint32_t, VkDeviceSize> sizePerType;
		for (const auto& pair : allocInfo)
			sizePerType[pair.second.memoryTypeIndex] += pair.second.allocationSize;

		for (const auto& pair : sizePerType)
			logging::log(LOGLV_INFO, true, "Type ", pair.first, ": ", pair.second, " B (", pair.second / 1024 / 1024, " MiB)");

		logging::log(LOGLV_INFO, true, "--------------------------");
	}
};

extern MemoryMonitor gMemMonitor;
#endif
