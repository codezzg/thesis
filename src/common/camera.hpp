#pragma once

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

// Thank you, windows
#undef near
#undef far

class CameraController;

struct Camera {
	glm::vec3 position = { 0, 0, 0 };
	glm::vec3 front;
	glm::vec3 up;
	glm::vec3 right;
	glm::vec3 worldUp = { 0, 1, 0 };
	float yaw = -90;
	float pitch = 0;
	// float fov = 45;
	// float ratio = 16.f / 9.f;
	// float near = 0.1f;
	// float far = 300.f;

	explicit Camera() { updateVectors(); }

	glm::mat4 viewMatrix() const;
	// glm::mat4 projMatrix() const;
	glm::vec4 forward() const;

	void updateVectors();
};

struct Frustum {
	glm::vec4 left;
	glm::vec4 right;
	glm::vec4 bottom;
	glm::vec4 top;
	glm::vec4 near;
	glm::vec4 far;
};

/** Given a matrix `m`, calculates the frustum planes.
 *  If `m` is the projection matrix, the clipping planes are in view space;
 *  if `m` is the modelviewproj matrix, they are in model space.
 *  @see http://web.archive.org/web/20120531231005/http://crazyjoke.free.fr/doc/3D/plane%20extraction.pdf
 */
Frustum calcFrustum(const glm::mat4& m);
