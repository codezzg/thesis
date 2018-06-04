#pragma once

#include "hashing.hpp"
#include "shared_resources.hpp"
#include <vector>

struct ModelInfo {
	StringId name;
	std::vector<StringId> materials;
	std::vector<shared::Mesh> meshes;
};
