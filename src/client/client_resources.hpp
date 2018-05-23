#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include "stack_allocator.hpp"
#include "shared_resources.hpp"
#include "hashing.hpp"

/** This class provides a RAII facility to store resources.
 *  The data is stored contiguously via a stack allocator, while the
 *  pointers to it are accessed via a map.
 *  Note that pointers retreived from said maps must not outlive this object,
 *  or the memory they point to will become invalid.
 */
class ClientResources final {
	std::vector<uint8_t> memory;

	StackAllocator allocator;

public:
	std::unordered_map<StringId, shared::Texture> textures;

	explicit ClientResources(std::size_t size)
		: memory(size)
		, allocator{ memory.data(), memory.size() }
	{}

	/** Copies the data pointed by `texture` into the internal memory.
	 *  The texture information is stored into `textures` with key `name`.
	 */
	void storeTexture(StringId name, shared::Texture texture) {
		void *ptr = allocator.alloc(texture.size);
		memcpy(ptr, texture.data, texture.size);
		texture.data = ptr;
		textures[name] = texture;
	}
};