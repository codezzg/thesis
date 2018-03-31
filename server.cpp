#include "server_endpoint.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include "config.hpp"

int main() {
	if (!Endpoint::init()) {
		std::cerr << "Failed to initialize sockets." << std::endl;
		return EXIT_FAILURE;
	}
	std::atexit([] () { Endpoint::cleanup(); });

	Server server;
	server.run(cfg::SERVER_ACTIVE_IP, cfg::SERVER_ACTIVE_PORT, cfg::SERVER_PASSIVE_IP, cfg::SERVER_PASSIVE_PORT);
	//ServerActiveEndpoint ep;
	//ep.startActive("0.0.0.0", 1234);
	//ep.runLoop();

	using namespace std::chrono_literals;
	std::this_thread::sleep_for(99999999s);
}
