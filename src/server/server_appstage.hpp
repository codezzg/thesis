#pragma once

#include "camera.hpp"
#include "frame_data.hpp"
#include "model.hpp"
#include "vertex.hpp"
#include <algorithm>
#include <array>
#include <glm/glm.hpp>

struct Server;

/** The procedures used by the server's "application stage", e.g. culling
 *  optimization, vertex transformations, etc.
 */

// STUB entrypoint for stuff happening on application stage
// `buffer` is the pointer to the memory where to store temporary frame data
// `nVertices` and `nIndices` get updated by this function
void transformVertices(Model& model,
	const std::array<uint8_t, FrameData().payload.size()>& clientData,
	/* inout */ uint8_t* buffer,
	std::size_t bufsize,
	/* inout */ int& nVertices,
	/* inout */ int& nIndices);

void appstageLoop(Server& server);

#ifdef _WIN32
inline bool sphereInFrustum(const glm::vec3& pos, float radius, const Frustum& frustum)
{
#else
constexpr bool sphereInFrustum(const glm::vec3& pos, float radius, const Frustum& frustum)
{
#endif
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
