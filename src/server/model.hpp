#pragma once

#include <vector>
#include "vertex.hpp"
#include "shared_resources.hpp"

struct Material {
	std::string name;

	std::string diffuseTex;
	std::string specularTex;
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

/** Loads a model's vertices and indices into `buffer`.
 *  `buffer` must be a region of correctly initialized memory.
 *  Upon success, `buffer` gets filled with [vertices|indices], and indices start at
 *  offset `sizeof(Vertex) * nVertices`.
 *  Will return a model with nullptr `vertices` if there were errors.
 */
Model loadModel(const char *modelPath, /* inout */ void *buffer);
