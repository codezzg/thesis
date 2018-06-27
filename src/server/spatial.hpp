#pragma once

#include "ext_mem_user.hpp"
#include "hashing.hpp"
#include "math_utils.hpp"
#include "pool_allocator.hpp"
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

struct Transform {
	const glm::vec3 position{ 0.f, 0.f, 0.f };
	const glm::quat rotation;
	const glm::vec3 scale{ 1.f, 1.f, 1.f };
};

constexpr Transform setPosition(Transform t, glm::vec3 pos)
{
	return Transform{ pos, t.rotation, t.scale };
}

inline Transform setRotation(Transform t, glm::vec3 euler)
{
	return Transform{ t.position, glm::quat{ euler }, t.scale };
}

constexpr Transform setScale(Transform t, glm::vec3 scale)
{
	return Transform{ t.position, t.rotation, scale };
}

inline glm::mat4 transformMatrix(Transform t)
{
	return glm::translate(
		glm::rotate(glm::scale(glm::mat4{ 1.f }, t.scale), quatAngle(t.rotation), quatAxis(t.rotation)),
		t.position);
}

enum class NodeType { EMPTY, MODEL, POINT_LIGHT };

/** A Node is a generic entity in the world with a 3D transform */
struct Node {
	/** This name points to a resource in server.resources */
	StringId name;
	NodeType type;
	Transform transform;

	Node* parent = nullptr;
};

/** The "world" */
struct Scene : public ExternalMemoryUser {
	PoolAllocator<Node> allocator;
	Node* root = nullptr;

private:
	void onInit() override { allocator.init(memory, memsize); }
};
