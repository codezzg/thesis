#include "memory.hpp"
#include <memory>

bool ApplicationMemory::reserve(std::size_t size) {
	if (memsize >= size)
		return true;

	//VkMemoryAllocateInfo allocInfo = {};
	//allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	//allocInfo.allocationSize = size;
	//allocInfo
	// FIXME
	mem = reinterpret_cast<uint8_t*>(realloc(mem, size));

	return !!mem;
}

uint8_t* ApplicationMemory::alloc(std::size_t size) {
	if (firstFree + size > mem + memsize)
		throw std::bad_alloc{};

	auto address = firstFree;
	firstFree += size;
	return address;
}
