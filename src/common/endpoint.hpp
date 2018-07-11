#pragma once

#include "endpoint_xplatform.hpp"
#include "tcp_messages.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

struct FrameData;
class BandwidthLimiter;

extern BandwidthLimiter gBandwidthLimiter;

// Common functions
bool sendPacket(socket_t socket, const uint8_t* data, std::size_t len);

/** Receives a packet from `socket`, storing at most `len` bytes into `buffer`.
 *  Buffer must be at least `len` bytes long. That is *NOT* checked by this function.
 *  If `bytesRead` is not null, it is filled with the actual number of bytes read.
 *  @return true if > 0 bytes were read, false otherwise.
 */
bool receivePacket(socket_t socket, uint8_t* buffer, std::size_t len, int* bytesRead = nullptr);

/** Checks whether the data contained in `packetBuf` conforms to our
 *  UDP protocol or not (i.e. has the proper header)
 */
bool validateUDPPacket(const uint8_t* packetBuf, uint32_t packetGen);

/** Receives a message from socket expecting it to be a TCP message conforming to
 *  our protocol, and fills both `buffer` with the entire packet receives and
 *  `type` with the TcpMsgType extracted from that message.
 */
bool receiveTCPMsg(socket_t socket, uint8_t* buffer, std::size_t len, TcpMsgType& type);

/** Like `receiveTCPMsg`, but returns true if and only if a message of type `type` was received. */
bool expectTCPMsg(socket_t socket, uint8_t* buffer, std::size_t len, TcpMsgType type);

/** Sends a header-only TCP message of type `type` */
bool sendTCPMsg(socket_t socket, TcpMsgType type);
//

struct Endpoint {
	enum class Type { ACTIVE, PASSIVE };

	socket_t socket = xplatInvalidSocketID();
	std::string ip;
	int port;
	bool connected;
};

Endpoint startEndpoint(const char* ip, int port, Endpoint::Type type, int socktype);
void closeEndpoint(Endpoint& ep);

class NetworkThread {
protected:
	std::thread thread;
};
