#pragma once

#include <cstddef>
#include <array>
#include "camera.hpp"

/** Serializes `camera` into the given `buffer`. */
template <std::size_t N>
inline void serializeCamera(std::array<uint8_t, N>& buffer, const Camera& camera) {
	static_assert(N * sizeof(uint8_t) >= 7 * sizeof(float),
		"serializeCamera: buffer given is too small! Should be able to contain at least 7 floats.");
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
	auto buf = buffer.data();
	*(reinterpret_cast<float*>(buf + 0)) = camera.position.x;
	*(reinterpret_cast<float*>(buf + 4)) = camera.position.y;
	*(reinterpret_cast<float*>(buf + 8)) = camera.position.z;
	*(reinterpret_cast<float*>(buf + 12)) = camera.rotation.w;
	*(reinterpret_cast<float*>(buf + 16)) = camera.rotation.x;
	*(reinterpret_cast<float*>(buf + 20)) = camera.rotation.y;
	*(reinterpret_cast<float*>(buf + 24)) = camera.rotation.z;
}

/** Deserializes a Camera out of given `buffer` */
template <std::size_t N>
inline Camera deserializeCamera(const std::array<uint8_t, N>& buffer) {
	static_assert(N * sizeof(uint8_t) >= 7 * sizeof(float),
		"deserializeCamera: buffer given is too small! Should be able to contain at least 7 floats.");

	Camera camera;
	const auto buf = buffer.data();
	camera.position.x = *reinterpret_cast<const float*>(buf + 0);
	camera.position.y = *reinterpret_cast<const float*>(buf + 4);
	camera.position.z = *reinterpret_cast<const float*>(buf + 8);
	camera.rotation.w = *reinterpret_cast<const float*>(buf + 12);
	camera.rotation.x = *reinterpret_cast<const float*>(buf + 16);
	camera.rotation.y = *reinterpret_cast<const float*>(buf + 20);
	camera.rotation.z = *reinterpret_cast<const float*>(buf + 24);
	return camera;
}
