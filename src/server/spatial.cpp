#include "spatial.hpp"
#include "logging.hpp"

using namespace logging;

// TODO: currently all nodes are children of root.
Node* Scene::addNode(StringId name, NodeType type, Transform transform)
{
	auto node = allocator.alloc();
	node->name = name;
	node->type = type;
	node->transform = transform;
	node->parent = root;
	nodes.emplace_back(node);
	nodeMap[name] = nodes.size() - 1;

	return node;
}

void Scene::destroyNode(StringId name)
{
	auto it = nodeMap.find(name);
	if (it == nodeMap.end()) {
		err("Tried to destroy inexistent node ", name);
		return;
	}
	assert(it->second < nodes.size());
	auto node = nodes[it->second];
	allocator.dealloc(node);
	nodes.erase(nodes.begin() + it->second);
	nodeMap.erase(it);
}

Node* Scene::getNode(StringId name) const
{
	auto it = nodeMap.find(name);
	if (it == nodeMap.end()) {
		return nullptr;
	}
	assert(it->second < nodes.size());
	return nodes[it->second];
}

void Scene::onInit()
{
	allocator.init(memory, memsize);
	// Allocate the root
	root = allocator.alloc();
	root->name = sid("__Scene_Root");
	root->type = NodeType::EMPTY;
	nodes.emplace_back(root);
	nodeMap[root->name] = nodes.size() - 1;
}

void Scene::clear()
{
	nodeMap.clear();
	nodes.clear();
	allocator.clear();
}
