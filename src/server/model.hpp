#pragma once

#include <vector>
#include <unordered_set>
#include "vertex.hpp"
#include "shared_resources.hpp"
#include "hashing.hpp"

struct Material {
	StringId name = SID_NONE;

	StringId diffuseTex = SID_NONE;
	StringId specularTex = SID_NONE;
};

struct Model {
	/** Unowning pointer to the model's vertices */
	Vertex *vertices = nullptr;
	/** Unowning pointer to the model's indices */
	Index *indices = nullptr;

	uint32_t nVertices = 0;
	uint32_t nIndices = 0;

	std::vector<Material> materials;

	std::size_t size() const {
		return nVertices * sizeof(Vertex) + nIndices * sizeof(Index);
	}
};

struct LoadedTextureInfo {
	std::string path;
	shared::TextureFormat format;
};

/** Loads a model's vertices and indices into `buffer`.
 *  `buffer` must be a region of correctly initialized memory.
 *  Upon success, `buffer` gets filled with [vertices|indices], and indices start at
 *  offset `sizeof(Vertex) * nVertices`.
 *  Moreover, `textureNames` gets filled with the paths to the textures that need to be loaded.
 *  These textures will correspond to some StringId in Model::materials::*Tex;
 *  @return a valid model, or one with nullptr `vertices` and `indices` if there were errors.
 */
Model loadModel(const char *modelPath, 
		/* inout */ void *buffer, 
		/* out */ std::unordered_set<std::string>& textureNames);
