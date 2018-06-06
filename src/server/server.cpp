#include "server.hpp"

Server::Server(std::size_t memsize)
        : memory(memsize)
        , activeEP{ *this }
        , passiveEP{ *this }
        , relEP{ *this }   // First 2/3 of server memory are for resources
        , resources{ memory.data(), memsize * 2 / 3 }
{
	// Last 1/3 of it is for the active EP's tmp buffer
	activeEP.initialize(memory.data() + memsize * 2 / 3, memsize / 3);
}

Server::~Server()
{
	closeNetwork();
}

void Server::closeNetwork()
{
	activeEP.close();
	passiveEP.close();
	relEP.close();
}
