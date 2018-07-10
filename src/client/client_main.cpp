/** @author Giacomo Parolini, 2018 */
#include "client.hpp"
#include "logging.hpp"
#include "xplatform.hpp"
#include <string>

using namespace logging;

bool gUseCamera = true;
bool gLimitFrameTime = true;

int main(int argc, char** argv)
{
	if (!xplatSocketInit()) {
		err("Failed to initialize sockets.");
		return EXIT_FAILURE;
	}
	if (!xplatEnableExitHandler()) {
		err("Failed to enable exit handler!");
		return EXIT_FAILURE;
	}
	xplatSetExitHandler([]() {
		if (xplatSocketCleanup())
			info("Successfully cleaned up sockets.");
		else
			err("Failed to cleanup sockets!");
	});

	// Parse args
	int i = argc - 1;
	std::string serverIp = "127.0.0.1";
	int posArgs = 0;
	while (i > 0) {
		if (strlen(argv[i]) < 2) {
			err("Invalid flag: -");
			return EXIT_FAILURE;
		}
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'c':
				gUseCamera = false;
				break;
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
			default:
				std::cout << "Usage: " << argv[0] << " [-c (use camera)] [-n (no colored logs)]\n";
				break;
			}
		} else {
			// Positional args: [serverIp]
			switch (posArgs++) {
			case 0:
				serverIp = std::string{ argv[i] };
				break;
			default:
				break;
			}
		}
		--i;
	}

	VulkanClient client;

	try {
		client.run(serverIp.c_str());
	} catch (const std::runtime_error& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
