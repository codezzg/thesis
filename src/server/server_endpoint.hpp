#pragma once

#include <mutex>
#include <condition_variable>
#include <chrono>
#include "endpoint.hpp"
#include "vertex.hpp"
#include "frame_data.hpp"

struct Server;
struct ServerSharedData;

/** This class implements the active server thread which sends geometry to client
 *  via an UDP socket.
 *  It will send data at regular fixed intervals determined by `targetFrameTime`.
 */
class ServerActiveEndpoint final : public Endpoint {

	Server& server;

	/** This memory is owned externally */
	uint8_t *memory;
	std::size_t memsize;

	void loopFunc() override;
	/** Sends vertices and indices, stored at `buffer`, to client */
	void sendFrameData(int64_t frameId, uint8_t *buffer, int nVertices, int nIndices);
public:
	std::chrono::milliseconds targetFrameTime;

	/** Constructs a ServerActiveEndpoint owned by `server`.
	 */
	explicit ServerActiveEndpoint(Server& server)
		: server{ server }
		, targetFrameTime{ std::chrono::milliseconds{ 33 } }
	{}

	/*  `memory` is a pointer into a valid memory buffer, which must be large enough to contain
	 *  the processed data (TODO: enforce this requirement).
	 */
	void initialize(uint8_t *mem, std::size_t size) {
		memory = mem;
		memsize = size;
	}
};

/** This class implements the passive server thread which receives client information
 *  (camera position, etc) during every frame.
 *  It will wait indefinitely on its UDP socket and send the data to the server's shared
 *  data memory as soon as possible.
 */
class ServerPassiveEndpoint final : public Endpoint {

	Server& server;

	void loopFunc() override;

public:
	explicit ServerPassiveEndpoint(Server& server)
		: server{ server }
	{}
};

/** This class implements a reliable connection server endpoint which handles the server-side
 *  reliable communication channel.
 *  It's used to perform initial handshake and as a keepalive for the client.
 */
class ServerReliableEndpoint : public Endpoint {

	Server& server;

	std::mutex loopMtx;
	std::condition_variable loopCv;

	void loopFunc() override;

	/** This method listens to an accepted connection coming from loopFunc.
	 *  It runs in a detached thread.
	 */
	void listenTo(socket_t clientSocket, sockaddr_in clientAddr);
	void onClose() override;

	bool sendOneTimeData(socket_t clientSocket);
	bool sendTexture(socket_t clientSocket, const std::string& name);

public:
	std::string serverIp;

	explicit ServerReliableEndpoint(Server& server)
		: server{ server }
	{}
};
