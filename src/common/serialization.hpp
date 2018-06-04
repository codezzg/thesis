#pragma once

#include "camera.hpp"
#include <array>
#include <cstddef>

/** Serializes `camera` into the given `buffer`. */
template <std::size_t N>
inline void serializeCamera(std::array<uint8_t, N>& buffer, const Camera& camera) {
	static_assert(N * sizeof(uint8_t) >= 5 * sizeof(float),
	        "serializeCamera: buffer given is too small! Should be able to contain at least 5 floats.");
	/*
	 * CameraData:
	 * [0] position.x
	 * [4] position.y
	 * [8] position.z
	 * [12] yaw
	 * [16] pitch
	 */
	auto buf = buffer.data();
	*(reinterpret_cast<float*>(buf + 0)) = camera.position.x;
	*(reinterpret_cast<float*>(buf + 4)) = camera.position.y;
	*(reinterpret_cast<float*>(buf + 8)) = camera.position.z;
	*(reinterpret_cast<float*>(buf + 12)) = camera.yaw;
	*(reinterpret_cast<float*>(buf + 16)) = camera.pitch;
}

/** Deserializes a Camera out of given `buffer` */
template <std::size_t N>
inline Camera deserializeCamera(const std::array<uint8_t, N>& buffer) {
	static_assert(N * sizeof(uint8_t) >= 5 * sizeof(float),
	        "deserializeCamera: buffer given is too small! Should be able to contain at least 5 floats.");

	Camera camera;
	const auto buf = buffer.data();
	camera.position.x = *reinterpret_cast<const float*>(buf + 0);
	camera.position.y = *reinterpret_cast<const float*>(buf + 4);
	camera.position.z = *reinterpret_cast<const float*>(buf + 8);
	camera.yaw = *reinterpret_cast<const float*>(buf + 12);
	camera.pitch = *reinterpret_cast<const float*>(buf + 16);

	return camera;
}
