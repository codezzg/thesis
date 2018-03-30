#pragma once

#include "vertex.hpp"

struct Model {
	/** Unowning pointer to the model's vertices */
	Vertex *vertices = nullptr;
	/** Unowning pointer to the model's indices */
	Index *indices = nullptr;
	int nVertices = 0;
	int nIndices = 0;
};

/** Loads a model's vertices and indices into `buffer`.
 *  `buffer` must be a region of correctly initialized memory.
 *  Upon success, `buffer` gets filled with [vertices|indices], and indices start at
 *  offset `sizeof(Vertex) * nVertices`.
 *  Will return a model with nullptr `vertices` if there were errors.
 */
Model loadModel(const char *modelPath, /* inout */ uint8_t *buffer);
