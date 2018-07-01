#pragma once

#include "math_utils.hpp"
#include <glm/glm.hpp>
#include <ostream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/transform.hpp>

struct Transform {
	glm::vec3 position{ 0.f, 0.f, 0.f };
	glm::quat rotation{ glm::vec3{ 0.f, 0.f, 0.f } };
	glm::vec3 scale{ 1.f, 1.f, 1.f };
};

inline std::ostream& operator<<(std::ostream& s, Transform t)
{
	s << "{ pos: " << glm::to_string(t.position) << ", rot: " << glm::to_string(t.rotation)
	  << ", scale: " << glm::to_string(t.scale) << " }";
	return s;
}

inline glm::mat4 transformMatrix(Transform t)
{
	return glm::translate(
		glm::rotate(glm::scale(glm::mat4{ 1.f }, t.scale), quatAngle(t.rotation), quatAxis(t.rotation)),
		t.position);
}

inline Transform transformFromMatrix(const glm::mat4& mat)
{
	Transform t;
	glm::vec3 ignoreSkew;
	glm::vec4 ignorePersp;
	glm::decompose(mat, t.scale, t.rotation, t.position, ignoreSkew, ignorePersp);
	return t;
}
