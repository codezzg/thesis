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

static constexpr std::size_t MEMSIZE = 1 << 25;

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
	std::unordered_set<std::string> texturesToLoad;
	auto model = server.resources.loadModel((cwd + xplatPath("/models/nanosuit/nanosuit.obj")).c_str(), texturesToLoad);
	if (model.vertices == nullptr) {
		err("Failed to load model.");
		return EXIT_FAILURE;
	}
	info("Loaded ", model.nVertices, " vertices + ", model.nIndices, " indices. ",
		"Tot size = ", (model.nVertices * sizeof(Vertex) + model.nIndices * sizeof(Index)) / 1024, " KiB");

	{
		auto& res = server.resources;

		// Load all needed textures into memory, mapping them by their SID.
		for (const auto& tex : texturesToLoad) {
			res.loadTexture(tex.c_str());
		}
		info("Loaded ", texturesToLoad.size(), " textures into memory.");
		// Free the memory
		std::unordered_set<std::string>().swap(texturesToLoad);

		// Assign the proper formats to the textures
		for (const auto& m : model.materials) {

			if (m.diffuseTex != SID_NONE) {
				auto it = res.textures.find(m.diffuseTex);
				assert(it != res.textures.end());

				auto& tex = it->second;
				assert(tex.format == shared::TextureFormat::UNKNOWN && "Overriding a valid texture format??");

				tex.format = shared::TextureFormat::RGBA;
			}

			if (m.specularTex != SID_NONE) {
				auto it = res.textures.find(m.specularTex);
				assert(it != res.textures.end());

				auto& tex = it->second;
				assert(tex.format == shared::TextureFormat::UNKNOWN && "Overriding a valid texture format??");

				tex.format = shared::TextureFormat::GREY;
			}
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
