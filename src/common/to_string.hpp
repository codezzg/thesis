#pragma once

#include <glm/gtx/string_cast.hpp>
#include <sstream>
#include <string>

inline std::string toString(const shared::PointLight& light)
{
	std::stringstream ss;
	ss << "PointLight { name = " << light.name << ", pos = " << glm::to_string(light.position)
	   << ", color = " << glm::to_string(light.color) << ", int = " << light.intensity
	   << ", dynMask = " << int(light.dynMask) << " }";
	return ss.str();
}
