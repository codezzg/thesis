#pragma once

#include "camera.hpp"
#include "model.hpp"
#include "vertex.hpp"
#include <algorithm>
#include <array>
#include <glm/glm.hpp>

struct Server;

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
