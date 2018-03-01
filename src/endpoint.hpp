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

	socket_t socket = invalidSocketID();
	bool passive;


	void loopPassive();
	void loopActive();

public:
	static bool init();
	static bool cleanup();

	bool startActive(const char *remoteIp, uint16_t remotePort);
	bool startPassive(const char *remoteIp, uint16_t remotePort);

	void runLoop();
	void close();
};
