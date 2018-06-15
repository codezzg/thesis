#pragma once

#include "client_resources.hpp"
#include "endpoint.hpp"
#include "vertex.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

struct Camera;
struct PayloadHeader;

/** This class implements the listening thread on the client which receives
 *  geometry data from the server. It listens indefinitely on an UDP socket
 *  and provides the client's rendering thread the frame data via the `peek`
 *  method.
 */
class ClientPassiveEndpoint : public Endpoint {

	uint8_t* buffer = nullptr;
	std::size_t usedBufSize = 0;

	std::mutex bufMtx;

	void loopFunc() override;

public:
	/* This method should be always called and verified to return true before calling `retreive`. */
	bool dataAvailable() const { return !terminated && usedBufSize > 0; }

	/** Copies the current `buffer` into `outBuf` in a thread-safe way.
	 *  @return the number of bytes copied.
	 */
	std::size_t retreive(uint8_t* outBuf, std::size_t outBufSize);
};

/** This class implements the client's active thread which sends miscellaneous per-frame data
 *  to the server (e.g. the camera position)
 */
class ClientActiveEndpoint : public Endpoint {
	const Camera* camera = nullptr;

	void loopFunc() override;

public:
	std::chrono::milliseconds targetFrameTime = std::chrono::milliseconds{ 33 };

	void setCamera(const Camera* camera) { this->camera = camera; }
};

/** This class implements the client side of the reliable communication channel, used
 *  for handshake and keepalive.
 */
class ClientReliableEndpoint : public Endpoint {

	void loopFunc() override;
	void onClose() override;

	bool receiveOneTimeData();

public:
	enum class ConnectionStage {
		HANDSHAKE,
		RECV_SRX,
		PREP_RSRC,
		SEND_RX_ACK,
		RECV_RSRC,
		CHECK_RSRC,
		LOAD_RSRC,
		START_UDP,
		SEND_READY,
		LISTENING,
		TERMINATING,
	};

	struct {
		std::mutex mtx;
		std::condition_variable cv;
		/** Used to guard cv against spurious wakeups */
		ConnectionStage stage;
	} coordination;

	/** This gets passed to us by the main thread, and remains valid
	 *  only during the one-time data exchange.
	 */
	ClientTmpResources* resources = nullptr;

	/** Call this after starting this socket to block the caller thread until the next step
	 *  in the protocol is performed or the timeout expires.
	 *  @return true if the handshake was completed before the timeout.
	 */
	bool await(std::chrono::seconds timeout);
	void proceed();

	bool disconnect();

	bool isConnected() const { return coordination.stage == ConnectionStage::LISTENING; }
};
