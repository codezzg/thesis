#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include "stack_allocator.hpp"
#include "shared_resources.hpp"
#include "hashing.hpp"

/** This class provides a RAII facility to store resources.
 *  Memory used by this class is owned and allocated by this own class.
 *  The data is stored contiguously via a stack allocator, while the
 *  pointers to it are accessed via a map.
 *  Note that pointers retreived from said maps must not outlive this object,
 *  or the memory they point to will become invalid.
 *  As the name implies, this class is designed to be a temporary stage
 *  for resources that won't be needed for long (typically the resources which
 *  must be received from the server and immediately transferred on the device,
 *  such as textures and shaders)
 */
class ClientTmpResources final {
	std::vector<uint8_t> memory;

public:
	StackAllocator allocator;

	std::unordered_map<StringId, shared::Texture> textures;
	std::unordered_map<StringId, shared::Material> materials;

	explicit ClientTmpResources(std::size_t size)
		: memory(size)
		, allocator{ memory.data(), memory.size() }
	{}
};
