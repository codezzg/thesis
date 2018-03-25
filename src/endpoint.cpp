#include "endpoint.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdexcept>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include <utility>
#include <cstring>
#include <functional>
#include "data.hpp"

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

Endpoint::~Endpoint() {
	close();
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

	return true;
}

bool Endpoint::startPassive(const char *remoteIp, uint16_t remotePort) {
	return start(remoteIp, remotePort, true);
}

bool Endpoint::startActive(const char *remoteIp, uint16_t remotePort) {
	return start(remoteIp, remotePort, false);
}

void Endpoint::runLoop() {
	std::cerr << "[" << this << "] called runLoop(). loopThead = " << loopThread.get() << "\n";
	if (loopThread)
		throw std::logic_error("Called runLoop twice on the same endpoint!");

	std::cout << "Starting loop with socket = " << socket << std::endl;
	loopThread = std::make_unique<std::thread>(std::bind(&Endpoint::loopFunc, this));
	terminated = false;
}

void Endpoint::close() {
	if (terminated)
		return;
	terminated = true;
	shutdown(socket, SHUT_RDWR);
	if (loopThread && loopThread->joinable())
		loopThread->join();
	loopThread.reset(nullptr);
}

bool receivePacket(socket_t socket, uint8_t *buffer, size_t len) {
	ssize_t count = recv(socket, buffer, len, 0);

	if (count < 0) {
		std::cerr << "Error receiving message: " << strerror(errno) << "\n";
		return false;
	} else if (count == sizeof(buffer)) {
		std::cerr << "Warning: datagram was truncated as it's too large.\n";
		return false;
	}

	//std::cerr << "Received " << count << " bytes: \n"; //<< buffer << std::endl;

	return true;
}


bool validatePacket(uint8_t *packetBuf, int64_t frameId) {
	const auto packet = reinterpret_cast<FrameData*>(packetBuf);
	if (packet->header.magic != cfg::PACKET_MAGIC) {
		std::cerr << "Packet has invalid magic: dropping.\n";
		return false;
	}
	if (packet->header.frameId < frameId) {
		std::cerr << "Packet is old: dropping\n";
		return false;
	}
	return true;
}

void dumpPacket(const char *fname, const FrameData& packet) {
	std::ofstream file(fname, std::ios::app);
	file << "\n--- packet " << packet.header.frameId << ":" << packet.header.packetId << "\n";
	file << "Header:\n";
	file << std::hex;
	for (int i = 0; i < sizeof(FrameHeader); ++i)
		file << (*(reinterpret_cast<const uint8_t*>(&packet.header) + i) & 0xFF) << " ";
	file << "\nPayload:\n";
	for (uint8_t byte : packet.payload) {
		file << (byte & 0xFF) << " ";
	}
}
