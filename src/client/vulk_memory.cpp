#include "vulk_memory.hpp"
#include "logging.hpp"
#include "vulk_errors.hpp"
#include <cassert>
#include <stdexcept>

using namespace logging;

#ifndef NDEBUG
MemoryMonitor gMemMonitor;

void MemoryMonitor::newAlloc(VkDeviceMemory memory, const VkMemoryAllocateInfo& aInfo) {
	++nAllocs;
	totSize += aInfo.allocationSize;
	allocInfo[memory] = aInfo;
	debug("--> New alloc type: ",
	        aInfo.memoryTypeIndex,
	        ", size: ",
	        aInfo.allocationSize,
	        " B (",
	        aInfo.allocationSize / 1024 / 1024,
	        " MiB)");
	report();
}

void MemoryMonitor::newFree(VkDeviceMemory memory) {
	++nFrees;
	const auto& aInfo = allocInfo[memory];
	totSize -= aInfo.allocationSize;
	debug("<-- new free type: ",
	        aInfo.memoryTypeIndex,
	        ", size: ",
	        aInfo.allocationSize,
	        " B (",
	        aInfo.allocationSize / 1024 / 1024,
	        " MiB)");
	allocInfo.erase(memory);
	report();
}

void MemoryMonitor::report() {
	log(LOGLV_DEBUG, true, "--------------------------");
	log(LOGLV_DEBUG,
	        true,
	        "# allocations so far: ",
	        nAllocs,
	        "\n# frees so far: ",
	        nFrees,
	        "\nTotal device mem used: ",
	        totSize,
	        " B (",
	        totSize / 1024 / 1024,
	        " MiB)");

	std::unordered_map<uint32_t, VkDeviceSize> sizePerType;
	for (const auto& pair : allocInfo)
		sizePerType[pair.second.memoryTypeIndex] += pair.second.allocationSize;

	for (const auto& pair : sizePerType)
		log(LOGLV_DEBUG,
		        true,
		        "Type ",
		        pair.first,
		        ": ",
		        pair.second,
		        " B (",
		        pair.second / 1024 / 1024,
		        " MiB)");

	log(LOGLV_DEBUG, true, "--------------------------");
}
#endif
