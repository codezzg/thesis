#include "config.hpp"
#include "hashing.hpp"
#include "logging.hpp"
#include "model.hpp"
#include "server.hpp"
#include "server_endpoint.hpp"
#include "units.hpp"
#include "xplatform.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <unordered_map>

using namespace logging;
using namespace std::literals::chrono_literals;

static constexpr std::size_t MEMSIZE = megabytes(32);
static constexpr auto CLIENT_UPDATE_TIME = std::chrono::milliseconds{ 33 };

Server* gServer;

static void parseArgs(int argc, char** argv, std::string& ip);
static bool loadAssets(Server& server);

int main(int argc, char** argv)
{

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
	xplatSetExitHandler([]() {
		gServer->closeNetwork();
		if (Endpoint::cleanup())
			info("Successfully cleaned up sockets.");
		else
			warn("Error cleaning up sockets: ", xplatGetErrorString());
	});

	Server server{ MEMSIZE };
	gServer = &server;

	server.activeEP.targetFrameTime = CLIENT_UPDATE_TIME;
	server.relEP.serverIp = ip;

	/// Startup server: load models, assets, etc
	if (!loadAssets(server)) {
		err("Failed loading assets!");
		return EXIT_FAILURE;
	}

	/// Start TCP socket and wait for connections
	server.relEP.startPassive(ip.c_str(), cfg::RELIABLE_PORT, SOCK_STREAM);
	server.relEP.runLoopSync();
}

void parseArgs(int argc, char** argv, std::string& ip)
{
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

bool loadAssets(Server& server)
{
	const auto cwd = xplatGetCwd();
	info("Starting server. cwd: ", cwd);

	// Load the models first: they'll remain at the bottom of our stack allocator
	// clang-format off
	auto model = server.resources.loadModel((cwd +
			  xplatPath("/models/nanosuit/nanosuit.obj")).c_str());
			 //xplatPath("/models/tiny/Tiny.obj")) .c_str());
			 //xplatPath("/models/cube/silver.obj")) .c_str());
	// xplatPath("/models/mill.obj")) .c_str());
	 //xplatPath("/models/cat/cat.obj")) .c_str());
	// xplatPath("/models/car/Avent.obj")) .c_str());
	// clang-format on

	if (model.vertices == nullptr) {
		err("Failed to load model.");
		return EXIT_FAILURE;
	}
	info("Loaded ",
		model.nVertices,
		" vertices + ",
		model.nIndices,
		" indices. ",
		"Tot size = ",
		(model.nVertices * sizeof(Vertex) + model.nIndices * sizeof(Index)) / 1024,
		" KiB");

	return true;
}
