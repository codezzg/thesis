#pragma once

#include "hashing.hpp"
#include "shared_resources.hpp"
#include <vector>

struct ModelInfo {
	std::vector<StringId> materials;
	std::vector<shared::Mesh> meshes;
	StringId name;
	uint32_t nVertices;
	uint32_t nIndices;
};
