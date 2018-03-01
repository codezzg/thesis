#pragma once

#include <cstdint>
#include <thread>
#include <memory>

using socket_t = int;

constexpr bool isValidSocket(socket_t sock) {
	return sock >= 0;
}

constexpr socket_t invalidSocketID() {
	return -1;
}

class Endpoint {
	std::unique_ptr<std::thread> loopThread;
	volatile bool terminated = false;

	socket_t socket = invalidSocketID();
	bool passive;


	bool start(const char *remoteIp, uint16_t remotePort, bool passive);
	void loopPassive();
	void loopActive();

public:
	// To be called once before using any Endpoint
	static bool init();
	// To be called once after closing all Endpoints
	static bool cleanup();

	bool startActive(const char *remoteIp, uint16_t remotePort);
	bool startPassive(const char *remoteIp, uint16_t remotePort);

	void runLoop();
	void close();
};
