#pragma once

#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#ifndef NDEBUG
class MemoryMonitor final {
	std::unordered_map<VkDeviceMemory, VkMemoryAllocateInfo> allocInfo;
	uint32_t nAllocs = 0;
	uint32_t nFrees = 0;
	VkDeviceSize totSize = 0;

public:
	void newAlloc(VkDeviceMemory memory, const VkMemoryAllocateInfo& info);
	void newFree(VkDeviceMemory memory);
	void report();
};

extern MemoryMonitor gMemMonitor;
#endif
