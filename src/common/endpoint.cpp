#include "endpoint.hpp"
#include <stdexcept>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <utility>
#include <cstring>
#include <string>
#include <functional>
#include "frame_data.hpp"
#include "logging.hpp"

using namespace logging;

static socket_t findFirstValidSocket(const addrinfo *result, socket_connect_op op) {
	// Connect
	for (auto info = result; info != nullptr; info = info->ai_next) {
		socket_t sock = ::socket(info->ai_family, info->ai_socktype, info->ai_protocol);
		if (!xplatIsValidSocket(sock))
			continue;

		if (op(sock, info->ai_addr, info->ai_addrlen) == 0)
			return sock;

		err("socket connect op failed with ", xplatGetErrorString(), " (", xplatGetError(), ") ");
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

bool Endpoint::start(const char *ip, uint16_t port, bool passive, int socktype) {

	addrinfo hints = {},
		 *result;
	hints.ai_family = AF_INET;
	hints.ai_socktype = socktype;
	if (passive)
		hints.ai_flags = AI_PASSIVE;

	auto res = getaddrinfo(ip, std::to_string(port).c_str(), &hints, &result);
	if (res != 0) {
		err("getaddrinfo: ", gai_strerror(res));
		return false;
	}

	socket = findFirstValidSocket(result, passive ? ::bind : ::connect);
	freeaddrinfo(result);

	if (!xplatIsValidSocket(socket)) {
		err("failed to connect to remote!");
		return false;
	}

	info("Endpoint: started ", (passive ? "passive" : "active"),
			" on ", ip, ":", port, " (type ", socktype, ")");

	this->ip = ip;
	this->port = port;

	return true;
}

bool Endpoint::startPassive(const char *ip, uint16_t port, int socktype) {
	return start(ip, port, true, socktype);
}

bool Endpoint::startActive(const char *ip, uint16_t port, int socktype) {
	return start(ip, port, false, socktype);
}

void Endpoint::runLoop() {
	debug("[", this, "] called runLoop(). loopThread = ", loopThread.get());
	if (loopThread)
		throw std::logic_error("Called runLoop twice on the same endpoint!");

	info("Starting loop with socket = ", socket);
	terminated = false;
	loopThread = std::make_unique<std::thread>(std::bind(&Endpoint::loopFunc, this));
}

void Endpoint::runLoopSync() {
	debug("[", this, "] called runLoopSync().");
	if (loopThread)
		throw std::logic_error("Endpoint is already running an async loop!");
	terminated = false;
	loopFunc();
}

void Endpoint::close() {
	if (terminated)
		return;
	onClose();
	terminated = true;
	if (xplatIsValidSocket(socket))
		xplatSockClose(socket);
	if (loopThread && loopThread->joinable())
		loopThread->join();
	loopThread.reset(nullptr);
}

bool receivePacket(socket_t socket, uint8_t *buffer, std::size_t len) {
	const auto count = recv(socket, reinterpret_cast<char*>(buffer), len, 0);

	if (count < 0) {
		err("Error receiving message: [" , count, "] ", xplatGetErrorString(), " (", xplatGetError(), ")");
		return false;
	} else if (count == sizeof(buffer)) {
		warn("Warning: datagram was truncated as it's too large.");
		return false;
	}

	verbose("Received ", count, " bytes"); //<< buffer, std::endl;

	return true;
}


bool validateUDPPacket(uint8_t *packetBuf, int64_t frameId) {
	const auto packet = reinterpret_cast<FrameData*>(packetBuf);
	if (packet->header.magic != cfg::PACKET_MAGIC) {
		info("Packet has invalid magic: dropping.");
		return false;
	}
	if (packet->header.frameId < frameId) {
		info("Packet is old: dropping");
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
		warn("could not write to remote: ", xplatGetErrorString(), " (", xplatGetError(), ")");
		return false;
	}
	verbose("Sent data ", data);
	return true;
}

/** Receives a message from `socket` into `buffer` and fills the `msgType` variable according to the
 *  type of message received (i.e. the message header)
 */
bool receiveTCPMsg(socket_t socket, uint8_t *buffer, std::size_t bufsize, MsgType& msgType) {

	msgType = MsgType::UNKNOWN;

	const auto count = recv(socket, reinterpret_cast<char*>(buffer), bufsize, 0);
	if (count < 0) {
		err("Error receiving message: [", count, "] ", xplatGetErrorString(), " G(", xplatGetError(), ")");
		return false;
	} else if (count == sizeof(buffer)) {
		warn("Warning: datagram was truncated as it's too large.");
		return false;
	} else if (count == 0) {
		warn("Received EOF.");
		return false;
	}

	// TODO: validate message header

	// Check type of message (TODO) -- currently the message type is determined by its first byte.
	msgType = byte2msg(buffer[0]);

	return true;
}

bool expectTCPMsg(socket_t socket, uint8_t *buffer, std::size_t bufsize, MsgType expectedType) {
	MsgType type;
	return receiveTCPMsg(socket, buffer, bufsize, type) && type == expectedType;
}
