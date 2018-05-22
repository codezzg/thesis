#include "server_endpoint.hpp"
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include "config.hpp"
#include "model.hpp"
#include "hashing.hpp"
#include "server.hpp"
#include "xplatform.hpp"
#include "logging.hpp"

using namespace logging;
using namespace std::literals::chrono_literals;

static constexpr auto MEMSIZE = 1 << 25;

Server *gServer;

static void parseArgs(int argc, char **argv, std::string& ip);

int main(int argc, char **argv) {

	std::string ip = "127.0.0.1";
	parseArgs(argc, argv, ip);

	std::cerr << "Debug level = " << static_cast<int>(gDebugLv) << "\n";

	/// Initial setup
	if (!Endpoint::init()) {
		std::cerr << "Failed to initialize sockets." << std::endl;
		return EXIT_FAILURE;
	}

	if (!xplatEnableExitHandler()) {
		std::cerr << "Failed to enable exit handler!\n";
		return EXIT_FAILURE;
	}
	xplatSetExitHandler([] () {
		gServer->closeNetwork();
		if (Endpoint::cleanup())
			info("Successfully cleaned up sockets.");
		else
			warn("Error cleaning up sockets: ", xplatGetErrorString());
	});

	Server server { MEMSIZE };
	gServer = &server;

	server.activeEP.targetFrameTime = 16ms;
	server.relEP.serverIp = ip;

	/// Startup server: load models, assets, etc
	std::string cwd = xplatGetCwd();
	info("Starting server. cwd: ", cwd);

	// Load the models first: they'll remain at the bottom of our stack allocator
	auto model = server.resources.loadModel((cwd + "/models/nanosuit/nanosuit.obj").c_str());
	if (model.vertices == nullptr) {
		err("Failed to load model.");
		return EXIT_FAILURE;
	}
	info("Loaded ", model.nVertices, " vertices + ", model.nIndices, " indices. ",
		"Tot size = ", (model.nVertices * sizeof(Vertex) + model.nIndices * sizeof(Index)) / 1024, " KiB");

	for (const auto& m : model.materials) {
		if (m.diffuseTex.length() > 0) {
			server.resources.loadTexture(m.diffuseTex.c_str(),
					shared::TextureFormat::RGBA);
		}
		if (m.specularTex.length() > 0) {
			server.resources.loadTexture(m.specularTex.c_str(),
					shared::TextureFormat::GREY);
		}
	}


	/// Start TCP socket and wait for connections
	server.relEP.startPassive(ip.c_str(), cfg::RELIABLE_PORT, SOCK_STREAM);
	server.relEP.runLoopSync();
}

void parseArgs(int argc, char **argv, std::string& ip) {
	int i = argc - 1;
	int posArgs = 0;
	while (i > 0) {
		if (strlen(argv[i]) < 2) {
			std::cerr << "Invalid flag -.\n";
			std::exit(EXIT_FAILURE);
		}
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'v': {
				int lv = 1;
				unsigned j = 2;
				while (j < strlen(argv[i]) && argv[i][j] == 'v') {
					++lv;
					++j;
				}
				gDebugLv = static_cast<LogLevel>(lv);
			} break;
			default:
				std::cerr << "Usage: " << argv[0] << " [-v[vvv...]]\n";
				std::exit(EXIT_FAILURE);
			}
		} else {
			switch (posArgs++) {
			case 0:
				ip = std::string{ argv[i] };
				break;
			default:
				break;
			}
		}
		--i;
	}
}
