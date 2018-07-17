#include "endpoint.hpp"
#include "bandwidth_limiter.hpp"
#include "frame_data.hpp"
#include "logging.hpp"
#include "udp_messages.hpp"
#include "utils.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

using namespace logging;

BandwidthLimiter gBandwidthLimiter;

static socket_t findFirstValidSocket(const addrinfo* result, socket_connect_op op)
{
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

Endpoint startEndpoint(const char* ip, int port, Endpoint::Type type, int socktype)
{
	Endpoint ep;

	addrinfo hints = {};
	addrinfo* result;
	hints.ai_family = AF_INET;
	hints.ai_socktype = socktype;
	if (type == Endpoint::Type::PASSIVE)
		hints.ai_flags = AI_PASSIVE;

	auto res = getaddrinfo(ip, std::to_string(port).c_str(), &hints, &result);
	if (res != 0) {
		err("getaddrinfo: ", gai_strerror(res));
		return ep;
	}

	auto socket = findFirstValidSocket(result, type == Endpoint::Type::PASSIVE ? ::bind : ::connect);
	freeaddrinfo(result);

	if (!xplatIsValidSocket(socket)) {
		err("failed to connect to remote!");
		return ep;
	}

	info("Endpoint: started ",
		(type == Endpoint::Type::PASSIVE ? "passive" : "active"),
		" on ",
		ip,
		":",
		port,
		" (type ",
		socktype,
		")");

	ep.socket = socket;
	ep.ip = ip;
	ep.port = port;
	ep.connected = true;

	return ep;
}

void closeEndpoint(Endpoint& ep)
{
	if (!ep.connected)
		return;

	ep.connected = false;
	if (xplatIsValidSocket(ep.socket)) {
		verbose("Endpoint: closing socket");
		const auto res = xplatSockClose(ep.socket);
		if (res != 0)
			warn("Error closing socket: ", xplatGetErrorString(), " (", xplatGetError(), ")");
	}
}

bool receivePacket(socket_t socket, uint8_t* buffer, std::size_t len, int* bytesRead)
{
	const auto count = ::recv(socket, reinterpret_cast<char*>(buffer), len, 0);
	if (bytesRead)
		*bytesRead = count;

	if (count < 0) {
		err("Error receiving message: [", count, "] ", xplatGetErrorString(), " (", xplatGetError(), ")");
		return false;
	} else if (count == 0) {
		warn("Received EOF");
		return false;
	}

	uberverbose("Received ", count, " bytes");
	if (gDebugLv >= LOGLV_UBER_VERBOSE)
		dumpBytes(buffer, count);

	return true;
}

bool validateUDPPacket(const uint8_t* packetBuf, uint32_t packetGen)
{
	const auto packet = reinterpret_cast<const UdpHeader*>(packetBuf);
	if (packet->packetGen < packetGen) {
		info("Packet is old: dropping");
		return false;
	}
	return true;
}

// These guys down here limit the amount of i/o spamming done by logging in the socket functions
static std::mutex spamMtx;
static std::chrono::time_point<std::chrono::system_clock> latestSpam;

static bool spamming(std::chrono::milliseconds spamTime = std::chrono::milliseconds(300))
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - latestSpam) <
	       spamTime;
}

static void spam()
{
	std::lock_guard<std::mutex> lock{ spamMtx };
	latestSpam = std::chrono::system_clock::now();
}

bool sendPacket(socket_t socket, const uint8_t* data, std::size_t len)
{
	while (!gBandwidthLimiter.requestTokens(len)) {
		std::unique_lock<std::mutex> ulk{ gBandwidthLimiter.cvMtx };
		verbose("Waiting for BandwidthLimiter (need ", len, ", has ", gBandwidthLimiter.getTokens(), ")");
		gBandwidthLimiter.cv.wait(ulk, [len]() {
			return !gBandwidthLimiter.isActive() ||
			       static_cast<std::size_t>(gBandwidthLimiter.getTokens()) >= len;
		});
	}
	// info("Out of while");

	if (::send(socket, reinterpret_cast<const char*>(data), len, 0) < 0) {
		if (!spamming()) {
			warn("could not write to remote: ", xplatGetErrorString(), " (", xplatGetError(), ")");
			spam();
		}
		return false;
	}
	uberverbose("Sent ", len, " bytes");
	if (gDebugLv >= LOGLV_UBER_VERBOSE)
		dumpBytes(data, len);

	return true;
}

/** Receives a message from `socket` into `buffer` and fills the `msgType` variable according to the
 *  type of message received (i.e. the message header)
 */
bool receiveTCPMsg(socket_t socket, uint8_t* buffer, std::size_t bufsize, TcpMsgType& msgType)
{
	msgType = TcpMsgType::UNKNOWN;

	if (!receivePacket(socket, buffer, bufsize))
		return false;

	// TODO: validate message header

	// Check type of message (TODO) -- currently the message type is determined by its first byte.
	msgType = byte2tcpmsg(buffer[0]);

	debug("<<< Received message type: ", msgType);

	return true;
}

bool expectTCPMsg(socket_t socket, uint8_t* buffer, std::size_t bufsize, TcpMsgType expectedType)
{
	TcpMsgType type;
	return receiveTCPMsg(socket, buffer, bufsize, type) && type == expectedType;
}

bool sendTCPMsg(socket_t socket, TcpMsgType type)
{
	const uint8_t b = tcpmsg2byte(type);
	const bool r = sendPacket(socket, &b, 1);
	if (r)
		debug(">>> Sent message type: ", type);
	else
		err("Failed to send message: ", type);

	return r;
}
