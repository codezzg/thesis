#pragma once

#include "hashing.hpp"
#include "logging.hpp"
#include "model.hpp"
#include "shared_resources.hpp"
#include "stack_allocator.hpp"
#include "utils.hpp"
#include <cassert>
#include <unordered_map>

/** This class manages the portion of server memory which stores resources, such as
 *  models and textures. Resources can have different lifespans: models are long-lived
 *  while textures and other one-time data are temporary (they only need to stay in memory
 *  until the server sends them to the client).
 *  This class uses a stack allocator to store permanent resources at its bottom and
 *  stashing temporary ones on its top, where they can easily be allocated and
 *  deallocated in a LIFO style.
 */
class ServerResources final {

	/** Server memory, owned externally */
	uint8_t* const memory = nullptr;
	const std::size_t memsize = 0;

public:
	/** Allocator containing the resources data. */
	StackAllocator allocator;

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
	explicit ServerResources(uint8_t* memory, std::size_t memsize)
	        : memory{ memory }
	        , memsize{ memsize }
	        , allocator{ memory, memsize } {}

	/** Loads a model from `file` into `memory` and stores its info in `models`.
	 *  @return The loaded Model information.
	 */
	Model loadModel(const char* file) {
		const auto fileSid = sid(file);
		if (models.count(fileSid) > 0) {
			logging::warn("Tried to load model ", file, " which is already loaded!");
			return models[fileSid];
		}

		// Reserve the whole remaining memory for loading the resource, then shrink to fit.
		auto buffer = allocator.allocAll();

		auto& model = models[fileSid];
		model = ::loadModel(file, buffer);

		allocator.deallocLatest();
		allocator.alloc(model.size());

		return model;
	}

	/** Loads a texture from `file` into `memory` and store its info in `textures`.
	 *  Does NOT set the texture format (in fact, it sets it to UNKNOWN)
	 *  @return The loaded Texture information
	 */
	shared::Texture loadTexture(const char* file) {
		const auto fileSid = sid(file);
		if (textures.count(fileSid) > 0) {
			logging::warn("Tried to load texture ", file, " which is already loaded!");
			return textures[fileSid];
		}

		std::size_t bufsize;
		auto buffer = allocator.allocAll(&bufsize);
		auto size = readFileIntoMemory(file, buffer, bufsize);

		assert(size > 0 && "Failed to load texture!");

		auto& texture = textures[fileSid];
		texture.size = size;
		texture.data = buffer;
		texture.format = shared::TextureFormat::UNKNOWN;

		allocator.deallocLatest();
		allocator.alloc(texture.size);

		logging::info("Loaded texture ", file, " (", texture.size / 1024., " KiB)");

		return texture;
	}
};
