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

	void loopFunc() override;
	void onClose() override;

public:
	struct {
		std::vector<uint32_t> list;
		std::mutex mtx;
		std::condition_variable cv;
	} acks;

	std::chrono::milliseconds targetFrameTime = std::chrono::milliseconds{ 33 };
	const Camera* camera = nullptr;
};

/** This class implements the client side of the reliable communication channel, used
 *  for handshake and keepalive.
 */
class ClientReliableEndpoint : public Endpoint {

	std::condition_variable keepaliveCv;

	bool connected = false;

	void loopFunc() override;
	void onClose() override;

public:
	bool disconnect();
	bool isConnected() const { return connected; }

	bool performHandshake();
	bool expectStartResourceExchange();
	bool sendRsrcExchangeAck();
	bool receiveOneTimeData(ClientTmpResources& resources);
	bool sendReadyAndWait();
};

struct ClientEndpoints {
	ClientPassiveEndpoint passive;
	ClientActiveEndpoint active;
	ClientReliableEndpoint reliable;
};
