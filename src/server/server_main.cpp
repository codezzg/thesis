#include "config.hpp"
#include "geom_update.hpp"
#include "hashing.hpp"
#include "logging.hpp"
#include "model.hpp"
#include "server.hpp"
#include "server_appstage.hpp"
#include "server_endpoint.hpp"
#include "units.hpp"
#include "xplatform.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <unordered_map>

using namespace logging;
using namespace std::literals::chrono_literals;

static constexpr std::size_t MEMSIZE = megabytes(32);
static constexpr auto CLIENT_UPDATE_TIME = std::chrono::milliseconds{ 33 };

Server* gServer;

static void parseArgs(int argc, char** argv, std::string& ip, std::size_t& limitBytesPerSecond);
static bool loadAssets(Server& server);

int main(int argc, char** argv)
{
	std::string ip = "127.0.0.1";
	std::size_t limitBytesPerSecond = 0;
	parseArgs(argc, argv, ip, limitBytesPerSecond);

	std::cerr << "Debug level = " << static_cast<int>(gDebugLv) << "\n";

	/// Initial setup
	if (!Endpoint::init()) {
		err("Failed to initialize sockets.");
		return EXIT_FAILURE;
	}
	if (limitBytesPerSecond > 0) {
		info("Limiting bandwidth to ", limitBytesPerSecond, " bytes/s");
		gBandwidthLimiter.setSendLimit(limitBytesPerSecond);
	}

	if (!xplatEnableExitHandler()) {
		err("Failed to enable exit handler!");
		return EXIT_FAILURE;
	}
	xplatSetExitHandler([]() {
		gServer->closeNetwork();
		gBandwidthLimiter.cleanup();
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

	// FIXME
	{
		shared::PointLight light;
		light.name = sid("Light 0");
		light.position = glm::vec3(10, 15, 0);
		light.color = glm::vec3(0.6, 0.0, 0.9);
		light.intensity = 1.5;
		light.dynMask = 0;
		server.resources.pointLights.emplace_back(light);
	}

	/// Start TCP socket and wait for connections
	server.relEP.startPassive(ip.c_str(), cfg::RELIABLE_PORT, SOCK_STREAM);
	server.relEP.runLoop();

	appstageLoop(server);
}

void parseArgs(int argc, char** argv, std::string& ip, std::size_t& limitBytesPerSecond)
{
	const auto usage = [argv]() {
		std::cerr << "Usage: " << argv[0]
			  << " [-v[vvv...]] [-n (no colored logs)] [-b (max bytes per second)]\n";
		std::exit(EXIT_FAILURE);
	};

	int i = 1;
	int posArgs = 0;
	while (i < argc) {
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
			case 'n':
				gColoredLogs = false;
				break;
			case 'b': {
				if (i == argc - 1) {
					usage();
				}
				auto bytesPerSeconds = std::atoll(argv[i + 1]);
				if (bytesPerSeconds <= 0)
					limitBytesPerSecond = 0;
				limitBytesPerSecond = static_cast<std::size_t>(bytesPerSeconds);
				++i;
			} break;
			default:
				usage();
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
		++i;
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
	 //xplatPath("/models/wall/wall2.obj")) .c_str());
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

	// Ensure all needed textures exist
	for (const auto& mat : model.materials) {
		if (mat.diffuseTex.length() > 0 && !std::ifstream(mat.diffuseTex)) {
			err("required texture ", mat.diffuseTex, " does not exist.");
			return false;
		}
		if (mat.specularTex.length() > 0 && !std::ifstream(mat.specularTex)) {
			err("required texture ", mat.specularTex, " does not exist.");
			return false;
		}
		if (mat.normalTex.length() > 0 && !std::ifstream(mat.normalTex)) {
			err("required texture ", mat.normalTex, " does not exist.");
			return false;
		}
	}

	return true;
}
