#pragma once

#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

constexpr float quatAngle(glm::quat q)
{
	return 2 * std::acos(q.w);
}

inline glm::vec3 quatAxis(glm::quat q)
{
	const auto d = std::sqrt(1.f - q.w * q.w);
	return q.w == 1 ? glm::vec3{ 1.f, 0.f, 0.f } : glm::vec3{ q.x / d, q.y / d, q.z / d };
}

