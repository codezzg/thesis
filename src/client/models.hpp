#pragma once

#include <vector>
#include "shared_resources.hpp"
#include "hashing.hpp"

struct ModelInfo {
	StringId name;
	std::vector<StringId> materials;
	std::vector<shared::Mesh> meshes;
};
