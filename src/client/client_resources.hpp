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

	StackAllocator allocator;

public:
	std::unordered_map<StringId, shared::Texture> textures;
	std::unordered_map<StringId, shared::Material> materials;

	explicit ClientTmpResources(std::size_t size)
		: memory(size)
		, allocator{ memory.data(), memory.size() }
	{}

	/** Copies the data pointed by `texture` into the internal memory.
	 *  The texture information is stored into `textures` with key `name`.
	 */
	void storeTexture(StringId name, const shared::Texture& texture) {
		void *ptr = allocator.alloc(texture.size);
		memcpy(ptr, texture.data, texture.size);
		auto& tex = textures[name];
		tex = texture;
		tex.data = ptr;
	}

	void storeMaterial(const shared::Material& material) {
		materials[material.name] = material;
	}
};
