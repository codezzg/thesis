#include "endpoint.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdexcept>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <utility>
#include <cstring>

using socket_connect_op = int (*) (int, const sockaddr*, socklen_t);

static socket_t findFirstValidSocket(const addrinfo *result, socket_connect_op op) {
	// Connect
	for (auto info = result; info != nullptr; info = info->ai_next) {
		socket_t sock = ::socket(info->ai_family, info->ai_socktype, info->ai_protocol);
		if (!isValidSocket(sock))
			continue;

		if (op(sock, info->ai_addr, info->ai_addrlen) == 0)
			return sock;

		close(sock);
	}

	return invalidSocketID();
}

// TODO: on windows, these ought to setup and cleanup sockets
bool Endpoint::init() {
	return true;
}

bool Endpoint::cleanup() {
	return true;
}

bool Endpoint::start(const char *remoteIp, uint16_t remotePort, bool passive) {

	addrinfo hints = {},
		 *result;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if (passive)
		hints.ai_flags = AI_PASSIVE;

	auto res = getaddrinfo(remoteIp, std::to_string(remotePort).c_str(), &hints, &result);
	if (res != 0) {
		std::cerr << "getaddrinfo: " << gai_strerror(res) << "\n";
		return false;
	}

	socket = findFirstValidSocket(result, passive ? ::bind : ::connect);
	freeaddrinfo(result);

	if (!isValidSocket(socket)) {
		std::cerr << "failed to connect to remote!" << std::endl;
		return false;
	}

	this->passive = passive;

	return true;
}

bool Endpoint::startPassive(const char *remoteIp, uint16_t remotePort) {
	return start(remoteIp, remotePort, true);
}

bool Endpoint::startActive(const char *remoteIp, uint16_t remotePort) {
	return start(remoteIp, remotePort, false);
}

// TODO
void Endpoint::loopPassive() {

	using namespace std::chrono_literals;

	// Receive datagrams
	while (true) {
		char buffer[1024];
		sockaddr_storage srcAddr;
		socklen_t srcAddrLen = sizeof(srcAddr);
		ssize_t count = recvfrom(socket, buffer, sizeof(buffer), 0,
				reinterpret_cast<sockaddr*>(&srcAddr), &srcAddrLen);

		if (count < 0) {
			std::cerr << "Error receiving message: " << strerror(errno) << "\n";
			break;
		} else if (count == sizeof(buffer)) {
			std::cerr << "Warning: datagram was truncated as it's too large.\n";
		}

		std::cerr << "Received: " << buffer << std::endl;

		std::this_thread::sleep_for(0.16s);
	}

	::close(socket);
}

// TODO
void Endpoint::loopActive() {

	using namespace std::chrono_literals;

	// Send datagrams
	while (true) {
		auto msg = "ping\n";

		if (write(socket, msg, strlen(msg)) < 0) {
			std::cerr << "could not write to remote: " << strerror(errno) << "\n";
		}

		std::this_thread::sleep_for(1s);
	}
}

void Endpoint::runLoop() {
	if (loopThread)
		throw std::logic_error("Called runLoop twice on the same endpoint!");

	std::cout << "Starting loop with socket = " << socket << std::endl;
	loopThread = std::make_unique<std::thread>(passive
			? std::bind(&Endpoint::loopPassive, this)
			: std::bind(&Endpoint::loopActive, this));
}

void Endpoint::close() {
	if (loopThread && loopThread->joinable())
		loopThread->join();
	loopThread.reset(nullptr);
}
