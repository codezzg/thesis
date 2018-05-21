#pragma once

#include <mutex>
#include <chrono>
#include <condition_variable>
#include "endpoint.hpp"
#include "vertex.hpp"

struct Camera;
struct PayloadHeader;

/** This class implements the listening thread on the client which receives
 *  geometry data from the server. It listens indefinitely on an UDP socket
 *  and provides the client's rendering thread the frame data via the `peek`
 *  method.
 */
class ClientPassiveEndpoint : public Endpoint {
	uint8_t *buffer = nullptr;
	uint64_t nVertices = 0;
	uint32_t nIndices = 0;
	volatile bool bufferFilled = false;
	volatile int64_t frameId = -1;

	std::mutex bufMtx;

	void loopFunc() override;

public:
	/** @return `true` if a buffer is complete available for processing, `false` otherwise.
	 *  This method should be always called and verified to return true before calling `retreive`.
	 */
	bool dataAvailable() const {
		return bufferFilled && !terminated;
	}

	/** Copies the current vertex data into `outVBuf`, the current index data into `outIBuf`
	 *  and the payload header into `phead` in a thread-safe way.
	 *  Results are undefined if dataAvailable() == false or either outVBuf or outIBuf's size
	 *  are less than BUFSIZE.
	 */
	void retreive(PayloadHeader& phead, Vertex *outVBuf, Index *outIBuf);

	/** @return The frame number corresponding to the latest completely filled buffer. */
	int64_t getFrameId() const { return frameId; }
};

/** This class implements the client's active thread which sends miscellaneous per-frame data
 *  to the server (e.g. the camera position)
 */
class ClientActiveEndpoint : public Endpoint {
	const Camera *camera = nullptr;

	void loopFunc() override;

public:
	std::chrono::milliseconds targetFrameTime = std::chrono::milliseconds{ 33 };

	void setCamera(const Camera *camera) { this->camera = camera; }
};


/** This class implements the client side of the reliable communication channel, used
 *  for handshake and keepalive.
 */
class ClientReliableEndpoint : public Endpoint {

	bool connected = false;

	std::condition_variable cv;

	void loopFunc() override;
	void onClose() override;
	bool receiveOneTimeData();

public:
	/** Call this after starting this socket to block the caller thread until the next step
	 *  in the protocol is performed or the timeout expires.
	 *  @return true if the handshake was completed before the timeout.
	 */
	bool await(std::chrono::seconds timeout);
	void proceed() { cv.notify_one(); }

	bool isConnected() const { return connected; }
};
