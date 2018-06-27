#pragma once

#include <cstddef>
#include <cstdint>

/** A subsystem which uses external memory.
 *  Must be initialized with a valid portion of memory which must not
 *  overlap with other ExternalMemoryUsers.
 */
class ExternalMemoryUser {
protected:
	/** This memory is owned externally */
	uint8_t* memory = nullptr;
	std::size_t memsize = 0;

	virtual void onInit() {}

public:
	void init(uint8_t* mem, std::size_t size)
	{
		memory = mem;
		memsize = size;
		onInit();
	}

	std::size_t getMemsize() const { return memsize; }
};

