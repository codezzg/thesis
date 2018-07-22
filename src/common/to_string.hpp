#pragma once

#include <ostream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

namespace shared {

inline std::ostream& operator<<(std::ostream& s, const shared::PointLight& light)
{
	s << "PointLight { name = " << light.name << ", color = " << glm::to_string(light.color)
	  << ", quad_atten = " << light.attenuation << " }";
	return s;
}

inline std::ostream& operator<<(std::ostream& s, shared::ShaderStage stage)
{
	switch (stage) {
		using S = shared::ShaderStage;
	case S::VERTEX:
		s << "Vertex";
		break;
	case S::FRAGMENT:
		s << "Fragment";
		break;
	case S::GEOMETRY:
		s << "Geometry";
		break;
	default:
		s << "Unknown";
		break;
	}
	return s;
}

}   // namespace shared
