#pragma once

#include <cassert>
#include <unordered_map>
#include "model.hpp"
#include "utils.hpp"
#include "hashing.hpp"
#include "logging.hpp"
#include "shared_resources.hpp"
#include "stack_allocator.hpp"

class ServerResources final {

	/** This memory is owned externally */
	uint8_t *memory = nullptr;
	std::size_t memsize = 0;

	/** Allocator containing the resources data */
	StackAllocator allocator;

public:
	/** Map { resource name => resource info }
	 *  The resource info contains pointers pointing to the actual data which
	 *  is inside `allocator`.
	 */
	std::unordered_map<StringId, Model> models;
	std::unordered_map<StringId, shared::Texture> textures;


	/** `memory` is a pointer into a valid buffer. The buffer should be large enough to contain all
	 *  resources and should not be manipulated by other than this class.
	 *  The buffer is freed externally, not by this class.
	 */
	explicit ServerResources(uint8_t *memory, std::size_t memsize)
		: allocator{ memory, memsize }
	{
		this->memory = memory;
		this->memsize = memsize;
	}

	/** Loads a model from `file` into our memory.
	 *  `texturesToLoad` is filled with paths to the textures used by the model.
	 *  @return The loaded Model information.
	 */
	Model loadModel(const char *file, std::unordered_set<std::string>& texturesToLoad) {
		// Reserve the whole remaining memory for loading the resource, then shrink to fit.
		auto buffer = allocator.allocAll();

		auto& model = models[sid(file)];
		model = ::loadModel(file, buffer, texturesToLoad);

		allocator.deallocLatest();
		allocator.alloc(model.size());

		return model;
	}

	/** Loads a texture from `file` into our memory.
	 *  Does NOT set the texture format (in fact, it sets it to UNKNOWN)
	 *  @return The loaded Texture information (data, size)
	 */
	shared::Texture loadTexture(const char *file) {
		std::size_t bufsize;
		auto buffer = allocator.allocAll(&bufsize);
		auto size = readFileIntoMemory(file, buffer, bufsize);

		assert(size > 0 && "Failed to load texture!");

		auto& texture = textures[sid(file)];
		texture.size = size;
		texture.data = buffer;
		texture.format = shared::TextureFormat::UNKNOWN;

		allocator.deallocLatest();
		allocator.alloc(texture.size);

		logging::info("Loaded texture ", file, " (", texture.size, " B)");

		return texture;
	}
};
