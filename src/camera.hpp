#pragma once

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <GLFW/glfw3.h>

struct Camera {
	glm::vec3 position = { 0, 0, 0 };
	glm::quat rotation = {};
	glm::vec3 worldUp = { 0, 1, 0 };
	float fov = 45;
	float ratio = 16.f / 9.f;
	float near = 0.1f;
	float far = 300.f;

	glm::mat4 viewMatrix() const;
	glm::mat4 projMatrix() const;
	glm::vec4 forward() const;
};

Camera createCamera();
