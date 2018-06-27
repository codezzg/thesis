#pragma once

#include "ext_mem_user.hpp"
#include "hashing.hpp"
#include "math_utils.hpp"
#include "pool_allocator.hpp"
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>
#include <memory>
#include <ostream>
#include <unordered_map>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

struct Transform {
	glm::vec3 position{ 0.f, 0.f, 0.f };
	glm::quat rotation{ glm::vec3{ 0.f, 0.f, 0.f } };
	glm::vec3 scale{ 1.f, 1.f, 1.f };
};

inline std::ostream& operator<<(std::ostream& s, Transform t)
{
	s << "{ pos: " << glm::to_string(t.position) << ", rot: " << glm::to_string(t.rotation)
	  << ", scale: " << glm::to_string(t.scale) << " }";
	return s;
}

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

/** A Scene is a graph of Nodes.
 *  Nodes are allocated from a pool which uses the server's main memory
 *  (helper data structures are allocated independently).
 */
struct Scene : public ExternalMemoryUser {

	/** Allows fast iteration on all nodes */
	std::vector<Node*> nodes;

	Node* root = nullptr;

	/** Adds node `name` of type `type` */
	void addNode(StringId name, NodeType type, Transform transform);

	/** Deallocates node `name` and removes it from the scene. */
	void destroyNode(StringId name);

	/** @return A pointer to node `name` or nullptr if that node is not in the scene. */
	Node* getNode(StringId name) const;

private:
	PoolAllocator<Node> allocator;
	/** Allows random access to nodes. Maps node name => node idx in the `nodes` vector. */
	std::unordered_map<StringId, uint64_t> nodeMap;

	void onInit() override;
};
