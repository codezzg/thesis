#include "camera.hpp"
#include "clock.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <immintrin.h>

void Camera::updateVectors() {
	front.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
	front.y = std::sin(glm::radians(pitch));
	front.z = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
	front = glm::normalize(front);
	right = glm::normalize(glm::cross(front, worldUp));
	up = glm::normalize(glm::cross(right, front));
}

glm::mat4 Camera::viewMatrix() const {
	return glm::lookAt(position, position + front, up);
}

//glm::mat4 Camera::projMatrix() const {
	//return glm::perspective(glm::radians(fov), ratio, near, far);
//}

Frustum calcFrustum(const glm::mat4& m) {
	Frustum frustum;

	// x = a, y = b, z = c, w = d (a, b, c, d are the plane coefficients)
	frustum.left.x = m[3][0] + m[0][0];
	frustum.left.y = m[3][1] + m[0][1];
	frustum.left.z = m[3][2] + m[0][2];
	frustum.left.w = m[3][3] + m[0][3];

	frustum.right.x = m[3][0] - m[0][0];
	frustum.right.y = m[3][1] - m[0][1];
	frustum.right.z = m[3][2] - m[0][2];
	frustum.right.w = m[3][3] - m[0][3];

	frustum.bottom.x = m[3][0] + m[1][0];
	frustum.bottom.y = m[3][1] + m[1][1];
	frustum.bottom.z = m[3][2] + m[1][2];
	frustum.bottom.w = m[3][3] + m[1][3];

	frustum.top.x = m[3][0] - m[1][0];
	frustum.top.y = m[3][1] - m[1][1];
	frustum.top.z = m[3][2] - m[1][2];
	frustum.top.w = m[3][3] - m[1][3];

	frustum.near.x = m[3][0] + m[2][0];
	frustum.near.y = m[3][1] + m[2][1];
	frustum.near.z = m[3][2] + m[2][2];
	frustum.near.w = m[3][3] + m[2][3];

	frustum.far.x = m[3][0] - m[2][0];
	frustum.far.y = m[3][1] - m[2][1];
	frustum.far.z = m[3][2] - m[2][2];
	frustum.far.w = m[3][3] - m[2][3];

	return frustum;
}
