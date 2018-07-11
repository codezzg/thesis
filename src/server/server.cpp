#include "server.hpp"
#include "cf_hashmap.hpp"
#include "logging.hpp"

using namespace logging;

/* Memory is used like this:
 * [66%] resources
 * [10%] scene
 * [05%] toClient.updates.persistent hashmap
 * [19%] active EP tmp buffer
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

	// Active EP's memory will be initialized later

	info("Server memory:\n- resources: ",
		resources.getMemsize() / 1024 / 1024,
		" MiB\n",
		"- scene: ",
		scene.getMemsize() / 1024 / 1024,
		" MiB\n",
		"- persistent updates: ",
		memsize / 20 / 1024 / 1024,
		" MiB\n",
		"- activeEP: ",
		allocator.remaining() / 1024 / 1024,
		" MiB");
}

Server::~Server()
{
	closeNetwork();
}

void Server::closeNetwork()
{
	closeEndpoint(endpoints.udpActive);
	closeEndpoint(endpoints.udpPassive);
	closeEndpoint(endpoints.tcpActive);
}
