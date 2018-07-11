#pragma once

#include "ext_mem_user.hpp"
#include "hashing.hpp"
#include "logging.hpp"
#include "model.hpp"
#include "shared_resources.hpp"
#include "stack_allocator.hpp"
#include "utils.hpp"
#include <cassert>
#include <unordered_map>
#include <unordered_set>

/** This class manages the portion of server memory which stores resources, such as
 *  models and textures. Resources can have different lifespans: models are long-lived
 *  while textures and other one-time data are temporary (they only need to stay in memory
 *  until the server sends them to the client).
 *  This class uses a stack allocator to store permanent resources at its bottom and
 *  stashing temporary ones on its top, where they can easily be allocated and
 *  deallocated in a LIFO style.
 */
struct ServerResources final : public ExternalMemoryUser {
	/** Allocator containing the resources data. */
	StackAllocator allocator;

	/** Map { resource name => resource info }
	 *  The resource info contains pointers pointing to the actual data which
	 *  is inside `allocator`.
	 */
	std::unordered_map<StringId, Model> models;
	std::unordered_map<StringId, shared::Texture> textures;
	std::unordered_map<StringId, shared::SpirvShader> shaders;
	/** These have no data inside `allocator`, they're stored "inline" in the map */
	std::vector<shared::PointLight> pointLights;

	/** Loads a model from `file` into `allocator` and stores its info in `models`.
	 *  @return The loaded Model information.
	 */
	Model loadModel(const char* file);

	/** Loads a texture from `file` into `allocator` and stores its info in `textures`.
	 *  Does NOT set the texture format (in fact, it sets it to UNKNOWN)
	 *  @return The loaded Texture information
	 */
	shared::Texture loadTexture(const char* file);

	/** Loads a shader from `file` into `allocator` and stores its info in `shaders`.
	 *  Does NOT set the shader stage or passNumber.
	 *  @return The loaded Shader information.
	 */
	shared::SpirvShader loadShader(const char* file);

private:
	void onInit() override { allocator.init(memory, memsize); }
};

/** A bunch of (unowned) references to existing resources. */
struct ResourceBatch {
	std::unordered_set<Model*> models;
	std::unordered_set<shared::SpirvShader*> shaders;
	std::unordered_set<shared::PointLight*> pointLights;
	// Note: this struct is currently only used by sendResourceBatch, which doesn't need
	// to save textures, so we don't save them here.
};
