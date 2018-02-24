#pragma once

//#define TINYOBJLOADER_IMPLEMENTATION
#include "third_party/tiny_obj_loader.h"
#include "Vertex.hpp"

//struct Model {
	//std::vector<Vertex> vertices;
	//std::vector<uint32_t> indices;
//};

void loadModel(const char *modelPath,
		/* out */ std::vector<Vertex>& vertices,
		/* out */ std::vector<uint32_t>& indices);
