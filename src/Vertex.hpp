#pragma once

#include <array>
#include <utility>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL // for hash function
#include <glm/gtx/hash.hpp>

struct Vertex final {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;

	static VkVertexInputBindingDescription getBindingDescription();

	static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();

	bool operator ==(const Vertex& other) const;
};

// Used for initializing empty key in sparsehash containers
constexpr Vertex VERTEX_EMPTY_KEY {
	glm::vec3 { -999999, -999999, -999999 },
	glm::vec3 { -999999, -999999, -999999 },
	glm::vec2 { -999999, -999999 }
};

namespace std {
	template<> struct hash<Vertex> {
		size_t operator ()(const Vertex& vertex) const {
			return ((hash<glm::vec3>()(vertex.pos) ^
				(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}
