#include "server_endpoint.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <unordered_map>
#include "config.hpp"
#include "model.hpp"
#include "hashing.hpp"
#include "server.hpp"

constexpr auto MEMSIZE = 1 << 24;

uint8_t *serverMemory;

Server *gServer;

int main() {
	/// Initial setup
	if (!Endpoint::init()) {
		std::cerr << "Failed to initialize sockets." << std::endl;
		return EXIT_FAILURE;
	}

	std::atexit([] () {
		gServer->closeNetwork();
		delete [] serverMemory;
		Endpoint::cleanup();
	});

	Server server;
	gServer = &server;

	/// Allocate server memory buffer
	serverMemory = new uint8_t[MEMSIZE];

	/// Initialize server subsystems
	// First 2/3 of server memory are for resources
	server.resources.initialize(serverMemory, MEMSIZE * 2 / 3);
	// Last 1/3 of it is for the active EP's tmp buffer
	server.activeEP.initialize(serverMemory + MEMSIZE * 2 / 3, MEMSIZE / 3);

	/// Startup server: load models, assets, etc
	std::cerr << "Starting server. cwd: " << xplatGetCwd() << std::endl;

	auto model = server.resources.loadModel((xplatGetCwd() + "/models/mill.obj").c_str());
	if (model.vertices == nullptr) {
		std::cerr << "Failed to load model.\n";
		return EXIT_FAILURE;
	}
	std::cerr << "Loaded " << model.nVertices << " vertices + " << model.nIndices << " indices. "
		<< "Tot size = " << (model.nVertices * sizeof(Vertex) + model.nIndices * sizeof(Index)) / 1024
		<< " KiB\n";


	/// Start TCP socket and wait for connections
	server.relEP.startPassive(cfg::SERVER_RELIABLE_IP, cfg::SERVER_RELIABLE_PORT, SOCK_STREAM);
	server.relEP.runLoop();



	//server.runNetwork(cfg::SERVER_ACTIVE_IP, cfg::SERVER_ACTIVE_PORT, cfg::SERVER_PASSIVE_IP, cfg::SERVER_PASSIVE_PORT);
	//ServerActiveEndpoint ep;
	//ep.startActive("0.0.0.0", 1234);
	//ep.runLoop();

	using namespace std::chrono_literals;
	std::this_thread::sleep_for(99999999s);
}
