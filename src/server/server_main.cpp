#include "bandwidth_limiter.hpp"
#include "config.hpp"
#include "geom_update.hpp"
#include "hashing.hpp"
#include "logging.hpp"
#include "model.hpp"
#include "server.hpp"
#include "server_appstage.hpp"
#include "server_tcp.hpp"
#include "server_udp.hpp"
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

static constexpr std::size_t MEMSIZE = megabytes(128);
static constexpr auto CLIENT_UPDATE_TIME = std::chrono::milliseconds{ 33 };

bool gMoveObjects = true;
bool gChangeLights = true;

struct MainArgs {
	std::string ip = "127.0.0.1";
	float limitBytesPerSecond = -1;
};

static void parseArgs(int argc, char** argv, MainArgs& args);
static bool loadAssets(Server& server);
std::vector<shared::PointLight> createLights(int n);

int main(int argc, char** argv)
{
	MainArgs args = {};
	parseArgs(argc, argv, args);

	std::cerr << "Debug level = " << static_cast<int>(gDebugLv) << "\n";

	/// Initial setup
	if (!xplatSocketInit()) {
		err("Failed to initialize sockets.");
		return EXIT_FAILURE;
	}
	if (args.limitBytesPerSecond >= 0) {
		info("Limiting bandwidth to ", args.limitBytesPerSecond, " bytes/s");
		gBandwidthLimiter.setSendLimit(args.limitBytesPerSecond);
		// gBandwidthLimiter.setMaxQueueingDelay(std::chrono::milliseconds{ 50 });
		gBandwidthLimiter.start();
	}

	Server server{ MEMSIZE };

	const auto atExit = [&server]() {
		debug("Sockets:\nudpActive: ",
			server.endpoints.udpActive.socket,
			"\nudpPassive: ",
			server.endpoints.udpPassive.socket,
			"\nreliable: ",
			server.endpoints.reliable.socket,
			"\nclient: ",
			server.networkThreads.keepalive->clientSocket);
		// "Ensure" we close the sockets even if we terminate abruptly
		gBandwidthLimiter.stop();
		server.closeNetwork();
		if (xplatSocketCleanup())
			info("Successfully cleaned up sockets.");
		else
			warn("Error cleaning up sockets: ", xplatGetErrorString());
		std::exit(0);
	};

	if (!xplatEnableExitHandler()) {
		err("Failed to enable exit handler!");
		return EXIT_FAILURE;
	}
	xplatSetExitHandler(atExit);

	/// Startup server: load models, assets, etc
	if (!loadAssets(server)) {
		err("Failed loading assets!");
		return EXIT_FAILURE;
	}

	{
		// Add lights
		const auto lights = createLights(10);
		server.resources.pointLights.insert(server.resources.pointLights.end(), lights.begin(), lights.end());
	}

	info("Filling spatial data structures...");
	/// Build and fill spatial data structures with the loaded objects
	for (const auto& pair : server.resources.models) {
		const auto& model = pair.second;
		server.scene.addNode(model.name, NodeType::MODEL, Transform{});
	}
	{
		auto sponza = server.scene.getNode(sid(xplatGetCwd() + xplatPath("/models/sponza/sponza.dae")));
		if (sponza)
			sponza->flags |= (1 << NODE_FLAG_STATIC);
	}

	for (const auto& light : server.resources.pointLights) {
		server.scene.addNode(light.name, NodeType::POINT_LIGHT, Transform{});
	}

	/// Start TCP socket and wait for connections
	server.endpoints.reliable =
		startEndpoint(args.ip.c_str(), cfg::RELIABLE_PORT, Endpoint::Type::PASSIVE, SOCK_STREAM);
	if (!xplatIsValidSocket(server.endpoints.reliable.socket)) {
		err("Failed to listen on ", args.ip, ":", cfg::RELIABLE_PORT, ": quitting.");
		return 1;
	}
	server.networkThreads.tcpActive = std::make_unique<TcpActiveThread>(server, server.endpoints.reliable);

	auto& toSend = server.networkThreads.tcpActive->resourcesToSend;

	std::this_thread::sleep_for(5s);
	{
		std::lock_guard<std::mutex> lock{ server.networkThreads.tcpActive->mtx };
		for (const auto& pair : server.resources.models)
			toSend.models.emplace(&pair.second);
		toSend.models.emplace(&server.resources.models.begin()->second);
	}
	server.networkThreads.tcpActive->cv.notify_one();
	std::this_thread::sleep_for(1s);
	//{
	// std::lock_guard<std::mutex> lock{ server.networkThreads.tcpActive->mtx };
	// for (const auto& pair : server.resources.shaders)
	// toSend.shaders.emplace(&pair.second);
	//}
	// server.networkThreads.tcpActive->cv.notify_one();
	// std::this_thread::sleep_for(1s);
	{
		std::lock_guard<std::mutex> lock{ server.networkThreads.tcpActive->mtx };
		for (const auto& light : server.resources.pointLights)
			toSend.pointLights.emplace(&light);
	}
	server.networkThreads.tcpActive->cv.notify_one();
	std::this_thread::sleep_for(2s);
	appstageLoop(server);
	atExit();
}

void parseArgs(int argc, char** argv, MainArgs& args)
{
	const auto usage = [argv]() {
		std::cerr << "Usage: " << argv[0] << " [-v[vvv...]] [-n (no colored logs)] [-b (max bytes per second)]"
			  << " [-m (don't move objects)] [-l (don't change lights)]\n";
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
				args.limitBytesPerSecond = std::atof(argv[i + 1]);
				++i;
			} break;

			case 'm':
				gMoveObjects = false;
				break;

			case 'l':
				gChangeLights = false;
				break;

			default:
				usage();
			}
		} else {
			switch (posArgs++) {
			case 0:
				args.ip = std::string{ argv[i] };
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

	//// Load the models first: they'll remain at the bottom of our stack allocator

	const auto loadSingleModel = [&](const char* name) {
		auto model = server.resources.loadModel((cwd + xplatPath(name)).c_str());

		if (model.vertices == nullptr) {
			err("Failed to load model.");
			return false;
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
	};

	//if (!loadSingleModel("/models/sponza/sponza.dae"))
		//return false;

	if (!loadSingleModel("/models/nanosuit/nanosuit.obj"))
		return false;

	//if (!loadSingleModel("/models/cube/silver.obj"))
	//	return false;

	// if (!loadSingleModel("/models/wall/wall2.obj"))
	// return false;

	// if (!loadSingleModel("/models/cat/cat.obj"))
	// return false

	return true;
}

std::vector<shared::PointLight> createLights(int n)
{
	std::vector<shared::PointLight> lights;
	lights.reserve(n);

	for (int i = 0; i < n; ++i) {
		shared::PointLight light;
		light.name = sid(std::string{ "Light " } + std::to_string(i));
		light.color = glm::vec3{ 1.0, 1.0, 1.0 };
		light.intensity = 1.0;
		lights.emplace_back(light);
	}

	return lights;
}
