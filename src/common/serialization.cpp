#include "serialization.hpp"

void serializeCamera(uint8_t *buffer, const Camera& camera) {
	/*
	 * CameraData:
	 * [0] position.x
	 * [4] position.y
	 * [8] position.z
	 * [12] rotation.w
	 * [16] rotation.x
	 * [20] rotation.y
	 * [24] rotation.z
	 */
	*(reinterpret_cast<float*>(buffer + 0)) = camera.position.x;
	*(reinterpret_cast<float*>(buffer + 4)) = camera.position.y;
	*(reinterpret_cast<float*>(buffer + 8)) = camera.position.z;
	*(reinterpret_cast<float*>(buffer + 12)) = camera.rotation.w;
	*(reinterpret_cast<float*>(buffer + 16)) = camera.rotation.x;
	*(reinterpret_cast<float*>(buffer + 20)) = camera.rotation.y;
	*(reinterpret_cast<float*>(buffer + 24)) = camera.rotation.z;
}

Camera deserializeCamera(const uint8_t *buffer) {
	Camera camera;
	camera.position.x = *reinterpret_cast<const float*>(buffer + 0);
	camera.position.y = *reinterpret_cast<const float*>(buffer + 4);
	camera.position.z = *reinterpret_cast<const float*>(buffer + 8);
	camera.rotation.w = *reinterpret_cast<const float*>(buffer + 12);
	camera.rotation.x = *reinterpret_cast<const float*>(buffer + 16);
	camera.rotation.y = *reinterpret_cast<const float*>(buffer + 20);
	camera.rotation.z = *reinterpret_cast<const float*>(buffer + 24);
	return camera;
}
