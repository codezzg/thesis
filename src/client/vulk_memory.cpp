#include "vulk_memory.hpp"
#include "vulk_errors.hpp"
#include <cassert>
#include <stdexcept>

#ifndef NDEBUG
MemoryMonitor gMemMonitor;

void MemoryMonitor::newAlloc(VkDeviceMemory memory, const VkMemoryAllocateInfo& info) {
	++nAllocs;
	totSize += info.allocationSize;
	allocInfo[memory] = info;
	logging::debug("--> New alloc type: ", info.memoryTypeIndex, ", size: ", info.allocationSize, " B (",
		info.allocationSize / 1024 / 1024, " MiB)");
	report();
}

void MemoryMonitor::newFree(VkDeviceMemory memory) {
	++nFrees;
	const auto& info = allocInfo[memory];
	totSize -= info.allocationSize;
	logging::debug("<-- new free type: ", info.memoryTypeIndex, ", size: ", info.allocationSize, " B (",
		info.allocationSize / 1024 / 1024, " MiB)");
	allocInfo.erase(memory);
	report();
}

void MemoryMonitor::report() {
	logging::log(LOGLV_DEBUG, true, "--------------------------");
	logging::log(LOGLV_DEBUG, true, "# allocations so far: ", nAllocs,
		"\n# frees so far: ", nFrees,
		"\nTotal device mem used: ", totSize, " B (", totSize / 1024 / 1024, " MiB)");

	std::unordered_map<uint32_t, VkDeviceSize> sizePerType;
	for (const auto& pair : allocInfo)
		sizePerType[pair.second.memoryTypeIndex] += pair.second.allocationSize;

	for (const auto& pair : sizePerType)
		logging::log(LOGLV_DEBUG, true, "Type ", pair.first, ": ", pair.second,
				" B (", pair.second / 1024 / 1024, " MiB)");

	logging::log(LOGLV_DEBUG, true, "--------------------------");
}
#endif
