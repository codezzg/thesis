#include "endpoint.hpp"
#include <stdexcept>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <utility>
#include <cstring>
#include <string>
#include <functional>
#include "data.hpp"

static socket_t findFirstValidSocket(const addrinfo *result, socket_connect_op op) {
	// Connect
	for (auto info = result; info != nullptr; info = info->ai_next) {
		socket_t sock = ::socket(info->ai_family, info->ai_socktype, info->ai_protocol);
		if (!xplatIsValidSocket(sock))
			continue;

		if (op(sock, info->ai_addr, info->ai_addrlen) == 0)
			return sock;

		std::cerr << "socket connect op failed with " << xplatGetErrorString()
			<< " (" << xplatGetError() << ") " << std::endl;
		xplatSockClose(sock);
	}

	return xplatInvalidSocketID();
}

bool Endpoint::init() {
	return xplatSocketInit();
}

bool Endpoint::cleanup() {
	return xplatSocketCleanup();
}

Endpoint::~Endpoint() {
	close();
}

bool Endpoint::start(const char *remoteIp, uint16_t remotePort, bool passive, int socktype) {

	addrinfo hints = {},
		 *result;
	hints.ai_family = AF_INET;
	hints.ai_socktype = socktype;
	if (passive)
		hints.ai_flags = AI_PASSIVE;

	auto res = getaddrinfo(remoteIp, std::to_string(remotePort).c_str(), &hints, &result);
	if (res != 0) {
		std::cerr << "getaddrinfo: " << gai_strerror(res) << "\n";
		return false;
	}

	socket = findFirstValidSocket(result, passive ? ::bind : ::connect);
	freeaddrinfo(result);

	if (!xplatIsValidSocket(socket)) {
		std::cerr << "failed to connect to remote!" << std::endl;
		return false;
	}

	return true;
}

bool Endpoint::startPassive(const char *remoteIp, uint16_t remotePort, int socktype) {
	return start(remoteIp, remotePort, true, socktype);
}

bool Endpoint::startActive(const char *remoteIp, uint16_t remotePort, int socktype) {
	return start(remoteIp, remotePort, false, socktype);
}

void Endpoint::runLoop() {
	std::cerr << "[" << this << "] called runLoop(). loopThread = " << loopThread.get() << "\n";
	if (loopThread)
		throw std::logic_error("Called runLoop twice on the same endpoint!");

	std::cout << "Starting loop with socket = " << socket << std::endl;
	loopThread = std::make_unique<std::thread>(std::bind(&Endpoint::loopFunc, this));
	terminated = false;
}

void Endpoint::close() {
	if (terminated)
		return;
	onClose();
	terminated = true;
	xplatSockClose(socket);
	if (loopThread && loopThread->joinable())
		loopThread->join();
	loopThread.reset(nullptr);
}

bool receivePacket(socket_t socket, uint8_t *buffer, std::size_t len) {
	const auto count = recv(socket, reinterpret_cast<char*>(buffer), len, 0);

	if (count < 0) {
		std::cerr << "Error receiving message: [" << count << "] " << xplatGetErrorString() << "\n";
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
	file << "\n--- packet " << packet.header.frameId << ":" << packet.header.packetId << "\n"
		<< "Header:\n" << std::hex;

	for (unsigned i = 0; i < sizeof(FrameHeader); ++i)
		file << (*(reinterpret_cast<const uint8_t*>(&packet.header) + i) & 0xFF) << " ";

	file << "\nPayload:\n";
	for (uint8_t byte : packet.payload) {
		file << (byte & 0xFF) << " ";
	}
}

bool sendPacket(socket_t socket, const uint8_t *data, std::size_t len) {
	if (::send(socket, reinterpret_cast<const char*>(data), len, 0) < 0) {
		std::cerr << "could not write to remote: " << xplatGetErrorString()
			<< " (" << xplatGetError() << ")\n";
		return false;
	}
	return true;
}
