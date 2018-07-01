#pragma once

#include <glm/gtx/string_cast.hpp>
#include <ostream>

inline std::ostream& toString(std::ostream& s, const shared::PointLight& light)
{
	s << "PointLight { name = " << light.name << ", color = " << glm::to_string(light.color)
	  << ", int = " << light.intensity << " }";
	return s;
}
