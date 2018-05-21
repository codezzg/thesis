#include "server.hpp"

Server::Server(std::size_t memsize)
	: memory{ new uint8_t[memsize] }
	, activeEP{ *this }
	, passiveEP{ *this }
	, relEP{ *this }
	// First 2/3 of server memory are for resources
	, resources{ memory.get(), memsize * 2 / 3 }
{
	// Last 1/3 of it is for the active EP's tmp buffer
	activeEP.initialize(memory.get() + memsize * 2 / 3, memsize / 3);
}

Server::~Server() {
	closeNetwork();
}

void Server::closeNetwork() {
	activeEP.close();
	passiveEP.close();
	relEP.close();
}
