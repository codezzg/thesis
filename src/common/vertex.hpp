#pragma once

#include "hashing.hpp"
#include <array>
#include <glm/glm.hpp>
#include <ostream>
#include <utility>

struct Vertex final {
	glm::vec3 pos;
	glm::vec3 norm;
	glm::vec2 texCoord;
	glm::vec3 tangent;
	glm::vec3 bitangent;

	bool operator==(const Vertex& other) const
	{
		return pos == other.pos && norm == other.norm && texCoord == other.texCoord &&
		       tangent == other.tangent && bitangent == other.bitangent;
	}
};

using Index = uint32_t;

namespace std {
template <>
struct hash<Vertex> {
	std::size_t operator()(const Vertex& vertex) const
	{
		// XXX: is this a good approach?
		return static_cast<std::size_t>(
			hashing::fnv1_hash(reinterpret_cast<const uint8_t*>(&vertex), sizeof(Vertex)));
	}
};
}   // namespace std

inline std::ostream& operator<<(std::ostream& s, const glm::vec2& v)
{
	s << "(" << v.x << ", " << v.y << ")";
	return s;
}

inline std::ostream& operator<<(std::ostream& s, const glm::vec3& v)
{
	s << "(" << v.x << ", " << v.y << ", " << v.z << ")";
	return s;
}

inline std::ostream& operator<<(std::ostream& s, const glm::vec4& v)
{
	s << "(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
	return s;
}

inline std::ostream& operator<<(std::ostream& s, const Vertex& v)
{
	s << v.pos << ", " << v.norm << ", " << v.texCoord;
	return s;
}
