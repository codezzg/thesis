#include "server.hpp"
#include "cf_hashmap.hpp"
#include "logging.hpp"

using namespace logging;

/* Memory is used like this:
 * [66%] resources
 * [10%] scene
 * [20%] active EP tmp buffer
 * [04%] toClient.updates.persistent hashmap
 */
Server::Server(std::size_t memsize)
	: memory(memsize)
	, activeEP{ *this }
	, passiveEP{ *this }
	, relEP{ *this }
{
	toClient.updates.transitory.reserve(1024);

	int64_t remainingMem = memsize;
	auto memptr = memory.data();
	resources.init(memptr, memsize * 2 / 3);
	remainingMem -= resources.getMemsize();
	memptr += resources.getMemsize();
	assert(remainingMem >= 0);

	scene.init(memptr, memsize / 10);
	remainingMem -= scene.getMemsize();
	memptr += scene.getMemsize();
	assert(remainingMem >= 0);

	activeEP.init(memptr, memsize / 5);
	remainingMem -= activeEP.getMemsize();
	memptr += activeEP.getMemsize();
	assert(remainingMem >= 0);

	toClient.updates.persistent = cf::hashmap<uint32_t, QueuedUpdate>::create(remainingMem, memptr);

	info("Server memory:\n- resources: ",
		resources.getMemsize() / 1024 / 1024,
		" MiB\n",
		"- scene: ",
		scene.getMemsize() / 1024 / 1024,
		" MiB\n",
		"- activeEP: ",
		activeEP.getMemsize() / 1024 / 1024,
		" MiB\n",
		"- persistent updates: ",
		remainingMem / 1024 / 1024,
		" MiB\n");

	assert(resources.getMemsize() + scene.getMemsize() + activeEP.getMemsize() <= memsize);
}

Server::~Server()
{
	closeNetwork();
}

void Server::closeNetwork()
{
	activeEP.close();
	passiveEP.close();
	info("Closing relEP...");
	relEP.close();
}
