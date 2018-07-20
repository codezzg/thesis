#include "server.hpp"
#include "cf_hashmap.hpp"
#include "logging.hpp"
#include "xplatform.hpp"
#include <fstream>

using namespace logging;

/* Memory is used like this:
 * [66%] resources
 * [10%] scene
 * [05%] toClient.updates.persistent hashmap
 */
Server::Server(std::size_t memsize)
	: memory(memsize)
{
	// Use a stack allocator to handle memory
	allocator.init(memory.data(), memsize);

	toClient.updates.transitory.reserve(1024);

	auto memptr = (uint8_t*)allocator.alloc(memsize * 2 / 3);
	resources.init(memptr, memsize * 2 / 3);

	memptr = (uint8_t*)allocator.alloc(memsize / 10);
	scene.init(memptr, memsize / 10);

	memptr = (uint8_t*)allocator.alloc(memsize / 20);
	toClient.updates.persistent = cf::hashmap<uint32_t, QueuedUpdate>::create(memsize / 20, memptr);

	info("Server memory:\n- resources: ",
		resources.getMemsize() / 1024 / 1024,
		" MiB\n",
		"- scene: ",
		scene.getMemsize() / 1024 / 1024,
		" MiB\n",
		"- persistent updates: ",
		memsize / 20 / 1024 / 1024,
		" MiB\n",
		"- remaining: ",
		allocator.remaining() / 1024 / 1024,
		" MiB");
}

Server::~Server()
{
	debug("~Server()");
	closeNetwork();
}

void Server::closeNetwork()
{
	info("Closing network");
	closeEndpoint(endpoints.reliable);
	if (networkThreads.tcpActive) {
		networkThreads.tcpActive->cv.notify_all();
		networkThreads.tcpActive.reset(nullptr);
	}
}

bool loadSingleModel(Server& server, const char* name, Model* outModel)
{
	auto model = server.resources.loadModel((server.cwd + xplatPath(name)).c_str());

	if (model.vertices == nullptr || model.data == nullptr) {
		err("Failed to load model.");
		return false;
	}
	info("Loaded ",
		model.nVertices,
		" vertices + ",
		model.nIndices,
		" indices. ",
		"Tot size = ",
		(model.nVertices * sizeof(Vertex) + model.nIndices * sizeof(Index)) / 1024,
		" KiB");

	// Ensure all needed textures exist
	for (const auto& mat : model.data->materials) {
		if (mat.diffuseTex.length() > 0 && !std::ifstream(mat.diffuseTex)) {
			err("required texture ", mat.diffuseTex, " does not exist.");
			return false;
		}
		if (mat.specularTex.length() > 0 && !std::ifstream(mat.specularTex)) {
			err("required texture ", mat.specularTex, " does not exist.");
			return false;
		}
		if (mat.normalTex.length() > 0 && !std::ifstream(mat.normalTex)) {
			err("required texture ", mat.normalTex, " does not exist.");
			return false;
		}
	}

	if (outModel)
		*outModel = model;

	return true;
}

