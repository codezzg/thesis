#include "server.hpp"

Server::Server()
	: activeEP{ *this }
	, passiveEP{ *this }
	, relEP{ *this }
{}

void Server::closeNetwork() {
	activeEP.close();
	passiveEP.close();
	relEP.close();
}
