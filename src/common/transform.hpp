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
private:
	glm::mat4 mat{ 1.f };

	glm::vec3 position{ 0.f, 0.f, 0.f };
	glm::quat rotation{ glm::vec3{ 0.f, 0.f, 0.f } };
	glm::vec3 scale{ 1.f, 1.f, 1.f };

	void _update()
	{
		mat = glm::translate(
			glm::rotate(glm::scale(glm::mat4{ 1.f }, scale), quatAngle(rotation), quatAxis(rotation)),
			position);
	}

public:
	void setPosition(const glm::vec3& pos)
	{
		position = pos;
		_update();
	}
	void setRotation(const glm::quat& q)
	{
		rotation = q;
		_update();
	}
	void setRotation(const glm::vec3& euler)
	{
		rotation = euler;
		_update();
	}
	void setScale(const glm::vec3& s)
	{
		scale = s;
		_update();
	}

	glm::mat4 getMatrix() const { return mat; }

	glm::vec3 getPosition() const { return position; }
	glm::quat getRotation() const { return rotation; }
	glm::vec3 getScale() const { return scale; }

	static Transform fromMatrix(const glm::mat4& mat)
	{
		Transform t;
		glm::vec3 ignoreSkew;
		glm::vec4 ignorePersp;
		glm::decompose(mat, t.scale, t.rotation, t.position, ignoreSkew, ignorePersp);
		return t;
	}
};

inline std::ostream& operator<<(std::ostream& s, Transform t)
{
	s << "{ pos: " << glm::to_string(t.getPosition()) << ", rot: " << glm::to_string(t.getRotation())
	  << ", scale: " << glm::to_string(t.getScale()) << " }";
	return s;
}
