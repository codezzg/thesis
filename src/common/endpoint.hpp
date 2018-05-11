#pragma once

#include <cstdint>
#include <thread>
#include <memory>
#include <array>
#include "endpoint_xplatform.hpp"

struct FrameData;

// Common functions
bool sendPacket(socket_t socket, const uint8_t *data, std::size_t len);

bool receivePacket(socket_t socket, uint8_t *buffer, std::size_t len);

bool validatePacket(uint8_t *packetBuf, int64_t frameId);

void dumpPacket(const char *fname, const FrameData& packet);
//

/** Base abstract class for a network endpoint running in a separate thread.
 *  Subclasses must implement `loopFunc`.
 */
class Endpoint {
private:
	std::unique_ptr<std::thread> loopThread = {};

	bool start(const char *remoteIp, uint16_t remotePort, bool passive, int socktype);

protected:
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
