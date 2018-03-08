#pragma once

#include <cstdint>
#include <thread>
#include <memory>
#include <array>

using socket_t = int;

constexpr bool isValidSocket(socket_t sock) {
	return sock >= 0;
}

constexpr socket_t invalidSocketID() {
	return -1;
}

class Endpoint {
private:
	std::unique_ptr<std::thread> loopThread;

	bool start(const char *remoteIp, uint16_t remotePort, bool passive);

protected:
	volatile bool terminated = false;
	socket_t socket = invalidSocketID();

	virtual void loopFunc() = 0;

public:
	// To be called once before using any Endpoint
	static bool init();
	// To be called once after closing all Endpoints
	static bool cleanup();

	virtual ~Endpoint();

	bool startActive(const char *remoteIp, uint16_t remotePort);
	bool startPassive(const char *remoteIp, uint16_t remotePort);

	void runLoop();
	void close();
};