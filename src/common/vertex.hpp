#pragma once

#include <array>
#include <utility>
#include <ostream>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL // for hash function
#include <glm/gtx/hash.hpp>


struct Vertex final {
	glm::vec3 pos;
	glm::vec3 norm;
	glm::vec2 texCoord;

	static VkVertexInputBindingDescription getBindingDescription();

	static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();

	bool operator ==(const Vertex& other) const;
};

using Index = uint32_t;

namespace std {
	template<> struct hash<Vertex> {
		std::size_t operator ()(const Vertex& vertex) const {
			return ((hash<glm::vec3>()(vertex.pos) ^
				(hash<glm::vec3>()(vertex.norm) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}

inline std::ostream& operator <<(std::ostream& s, const glm::vec2& v) {
	s << "(" << v.x << ", " << v.y << ")";
	return s;
}

inline std::ostream& operator <<(std::ostream& s, const glm::vec3& v) {
	s << "(" << v.x << ", " << v.y << ", " << v.z << ")";
	return s;
}

inline std::ostream& operator <<(std::ostream& s, const glm::vec4& v) {
	s << "(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
	return s;
}

inline std::ostream& operator <<(std::ostream& s, const Vertex& v) {
	s << v.pos << ", " << v.norm << ", " << v.texCoord;
	return s;
}
