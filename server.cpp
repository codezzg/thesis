#include "server_endpoint.hpp"
#include <chrono>
#include <thread>

int main() {
	ServerEndpoint ep;
	ep.startActive("0.0.0.0", 1234);
	ep.runLoop();

	using namespace std::chrono_literals;
	std::this_thread::sleep_for(999999s);
}
