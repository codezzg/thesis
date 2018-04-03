#pragma once

class ApplicationMemory final {
	uint8_t *mem = nullptr;
	std::size_t memsize = 0;
	uint8_t *firstFree = nullptr;

public:
	/** Request that at least `size` bytes be reserved for the memory pool. */
	bool reserve(std::size_t size);

	/** Request `size` bytes of memory. Will throw if the pool is not large enough. */
	uint8_t* alloc(std::size_t size);
};
