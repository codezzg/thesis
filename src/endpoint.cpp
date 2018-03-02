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
#include "Vertex.hpp"
#include <vector>

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

	buffer = (uint8_t*)malloc(BUFSIZE);

	// Receive datagrams
	while (!terminated) {
		//char buffer[1024];
		//memset(buffer, 0, BUFSIZE);
		sockaddr_storage srcAddr;
		socklen_t srcAddrLen = sizeof(srcAddr);
		ssize_t count = recvfrom(socket, buffer, 1024, 0,
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
const std::vector<Vertex> VERTICES = {
    {{0.0f, -0.5f, 0}, {1.0f, 0.0f, 0.0f}, {0, 1}},
    {{0.5f, 0.5f, 0}, {0.0f, 1.0f, 0.0f}, {1, 1}},
    {{-0.5f, 0.5f, 0}, {0.0f, 0.0f, 1.0f}, {0, 0}}
};
const std::vector<Index> INDICES = {
    0, 1, 2, 2, 3, 0
};
void Endpoint::loopActive() {

	using namespace std::chrono_literals;

	constexpr uint32_t MAGIC = 0x14101991;
	uint64_t packetId = 0;

	// Send datagrams
	while (!terminated) {
		/* [0] MAGIC            : uint32
		 * [4] ID               : uint64
		 * [12] NVertices       : uint64
		 * [20] Vertex0.pos.x   : float
		 * [24] Vertex0.pos.y   : float
		 * [28] Vertex0.pos.z   : float
		 * [32] Vertex0.color.x : float
		 * [36] Vertex0.color.y : float
		 * [40] Vertex0.tex.u   : float
		 * [42] Vertex0.tex.v   : float
		 * ...
		 * [20+NVertices*sizeof(Vertex)] NIndices : uint64
		 * [..] Index0 : uint32_t
		 * ...
		 */
		uint8_t message[1024];
		*((uint32_t*) message) = MAGIC;
		*((uint64_t*)(message + 4)) = packetId;
		*((uint64_t*)(message + 12)) = uint64_t(VERTICES.size());
		for (unsigned i = 0; i < VERTICES.size(); ++i) {
			*((Vertex*)(message + 20 + sizeof(Vertex)*i)) = VERTICES[i];
		}
		const auto indexOff = 20 + VERTICES.size() * sizeof(Vertex);
		*((uint64_t*)(message + indexOff)) = uint64_t(INDICES.size());
		for (unsigned i = 0; i < INDICES.size(); ++i) {
			*((uint32_t*)(message + 8 + indexOff + sizeof(Index)*i)) = INDICES[i];
		}

		std::cerr << "Useful bytes: " << (8 + indexOff + INDICES.size() * sizeof(Index)) << "\n";

		if (write(socket, message, 1024) < 0) {
			std::cerr << "could not write to remote: " << strerror(errno) << "\n";
		}

		++packetId;

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
	terminated = false;
}

void Endpoint::close() {
	terminated = true;
	if (loopThread && loopThread->joinable())
		loopThread->join();
	loopThread.reset(nullptr);
}
