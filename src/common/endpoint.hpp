#pragma once

#include "endpoint_xplatform.hpp"
#include "tcp_messages.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

struct FrameData;
struct BandwidthLimiter;

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

/** Base abstract class for a network endpoint running in a separate thread.
 *  Subclasses must implement `loopFunc`.
 */
class Endpoint {
private:
	std::unique_ptr<std::thread> loopThread = {};

	bool start(const char* remoteIp, uint16_t remotePort, bool passive, int socktype);

protected:
	/** address this endpoint was started on */
	std::string ip;
	/** port this endpoint was started on */
	int port;

	bool terminated = false;
	socket_t socket = xplatInvalidSocketID();

	/** The function running in this endpoint's thread. Should implement a loop
	 *  like `while (!terminated) { ... }`
	 */
	virtual void loopFunc() = 0;

	/** Optional callback that is called at the beginning of `close()` */
	virtual void onClose() {}

public:
	// To be called once before using any Endpoint
	static bool initEP();
	// To be called once after closing all Endpoints
	static bool cleanupEP();

	virtual ~Endpoint();

	/** Creates an active socket to `remoteIp`:`remotePort`.
	 *  If a previous socket exists, it will be overridden by the new one
	 *  (`close` should be called before starting a new socket)
	 *  @return true if the socket creation was successful.
	 */
	bool startActive(const char* remoteIp, uint16_t remotePort, int socktype);

	/** Creates a passive socket to `remoteIp`:`remotePort`.
	 *  If a previous socket exists, it will be overridden by the new one
	 *  (`close` should be called before starting a new socket)
	 *  @return true if the socket creation was successful.
	 */
	bool startPassive(const char* remoteIp, uint16_t remotePort, int socktype);

	/** Starts the `loopFunc` in a new thread. */
	void runLoop();

	/** Starts the `loopFunc` in the current thread. */
	void runLoopSync();

	/** Terminates the loop and closes the socket */
	void close();
};
