#pragma once

#include <cstdint>
#include <thread>
#include <memory>
#include <array>
#include "endpoint_xplatform.hpp"
#include "tcp_messages.hpp"

struct FrameData;

// Common functions
bool sendPacket(socket_t socket, const uint8_t *data, std::size_t len);

bool receivePacket(socket_t socket, uint8_t *buffer, std::size_t len);

/** Checks whether the data contained in `packetBuf` conforms to our
 *  UDP protocol or not (i.e. has the proper header)
 */
bool validateUDPPacket(uint8_t *packetBuf, int64_t frameId);

/** Dumps the content of `packet` into file `fname` */
void dumpPacket(const char *fname, const FrameData& packet);

/** Receives a message from socket expecting it to be a TCP message conforming to
 *  our protocol, and fills both `buffer` with the entire packet receives and
 *  `type` with the MsgType extracted from that message.
 */
bool receiveTCPMsg(socket_t socket, uint8_t *buffer, std::size_t len, MsgType& type);

/** Like `receiveTCPMsg`, but returns true if and only if a message of type `type` was received. */
bool expectTCPMsg(socket_t socket, uint8_t *buffer, std::size_t len, MsgType type);

/** Sends a header-only TCP message of type `type` */
bool sendTCPMsg(socket_t socket, MsgType type);
//

/** Base abstract class for a network endpoint running in a separate thread.
 *  Subclasses must implement `loopFunc`.
 */
class Endpoint {
private:
	std::unique_ptr<std::thread> loopThread = {};

	bool start(const char *remoteIp, uint16_t remotePort, bool passive, int socktype);

protected:
	/** address this endpoint was started on */
	std::string ip;
	/** port this endpoint was started on */
	int port;

	volatile bool terminated = false;
	socket_t socket = xplatInvalidSocketID();

	/** The function running in this endpoint's thread. Should implement a loop
	 *  like `while (!terminated) { ... }`
	 */
	virtual void loopFunc() = 0;

	/** Optional callback that is called at the beginning of `close()` */
	virtual void onClose() {}

public:
	// To be called once before using any Endpoint
	static bool init();
	// To be called once after closing all Endpoints
	static bool cleanup();

	virtual ~Endpoint();

	/** Creates an active socket to `remoteIp`:`remotePort`.
	 *  If a previous socket exists, it will be overridden by the new one
	 *  (`close` should be called before starting a new socket)
	 *  @return true if the socket creation was successful.
	 */
	bool startActive(const char *remoteIp, uint16_t remotePort, int socktype);

	/** Creates a passive socket to `remoteIp`:`remotePort`.
	 *  If a previous socket exists, it will be overridden by the new one
	 *  (`close` should be called before starting a new socket)
	 *  @return true if the socket creation was successful.
	 */
	bool startPassive(const char *remoteIp, uint16_t remotePort, int socktype);

	/** Starts the `loopFunc` in a new thread. */
	void runLoop();

	/** Starts the `loopFunc` in the current thread. */
	void runLoopSync();

	/** Terminates the loop and closes the socket */
	void close();
};
