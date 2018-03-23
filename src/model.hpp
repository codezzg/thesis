#pragma once

#include <vector>
#include "Vertex.hpp"

//struct Model {
	//std::vector<Vertex> vertices;
	//std::vector<uint32_t> indices;
//};

/** Loads a model's vertices and indices into `buffer`.
 *  Buffer is filled with [vertices|indices], and indices start at
 *  offset `sizeof(Vertex) * nVertices`.
 *  Will return false if there were errors.
 */
bool loadModel(const char *modelPath,
		/* out */ uint8_t *buffer,
		/* out */ int& nVertices,
		/* out */ int& nIndices);
