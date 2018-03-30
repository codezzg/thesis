#pragma once

#include <array>
#include <algorithm>
#include <glm/glm.hpp>
#include "vertex.hpp"
#include "data.hpp"
#include "camera.hpp"
#include "model.hpp"

/** The procedures used by the server's "application stage", e.g. culling
 *  optimization, vertex transformations, etc.
 */

// STUB entrypoint for stuff happening on application stage
// `buffer` is the pointer to the memory where to store temporary frame data
// `nVertices` and `nIndices` get updated by this function
void transformVertices(Model& model, const std::array<uint8_t, FrameData().payload.size()>& clientData,
		/* inout */ uint8_t *buffer, /* inout */ int& nVertices, /* inout */ int& nIndices);


constexpr bool sphereInFrustum(const glm::vec3& pos, float radius, const Frustum& frustum) {
	{
		const auto& plane = frustum.left;
		if (plane.x * pos.x + plane.y * pos.y + plane.z * pos.z + plane.w <= -radius)
			return false;
	}
	{
		const auto& plane = frustum.right;
		if (plane.x * pos.x + plane.y * pos.y + plane.z * pos.z + plane.w <= -radius)
			return false;
	}
	{
		const auto& plane = frustum.bottom;
		if (plane.x * pos.x + plane.y * pos.y + plane.z * pos.z + plane.w <= -radius)
			return false;
	}
	{
		const auto& plane = frustum.top;
		if (plane.x * pos.x + plane.y * pos.y + plane.z * pos.z + plane.w <= -radius)
			return false;
	}
	{
		const auto& plane = frustum.near;
		if (plane.x * pos.x + plane.y * pos.y + plane.z * pos.z + plane.w <= -radius)
			return false;
	}
	{
		const auto& plane = frustum.far;
		if (plane.x * pos.x + plane.y * pos.y + plane.z * pos.z + plane.w <= -radius)
			return false;
	}
	return true;
}
