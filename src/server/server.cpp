#include "server_endpoint.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include "config.hpp"

ServerReliableEndpoint rep;
Server server;

int main() {
	if (!Endpoint::init()) {
		std::cerr << "Failed to initialize sockets." << std::endl;
		return EXIT_FAILURE;
	}

	std::atexit([] () {
		rep.close();
		server.close();
		Endpoint::cleanup();
	});

	rep.startPassive(cfg::SERVER_RELIABLE_IP, cfg::SERVER_RELIABLE_PORT, SOCK_STREAM);
	rep.runLoop();

	server.run(cfg::SERVER_ACTIVE_IP, cfg::SERVER_ACTIVE_PORT, cfg::SERVER_PASSIVE_IP, cfg::SERVER_PASSIVE_PORT);
	//ServerActiveEndpoint ep;
	//ep.startActive("0.0.0.0", 1234);
	//ep.runLoop();

	using namespace std::chrono_literals;
	std::this_thread::sleep_for(99999999s);
}
