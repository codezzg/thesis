#pragma once

#include <mutex>
#include <condition_variable>
#include <chrono>
#include "endpoint.hpp"
#include "vertex.hpp"
#include "data.hpp"

class Server;

/** This class implements the active server thread which sends geometry to client
 *  via an UDP socket.
 *  It will send data at regular fixed intervals determined by `targetFrameTime`.
 */
class ServerActiveEndpoint : public Endpoint {
	Server& server;
	uint8_t *serverMemory = nullptr;

	void loopFunc() override;
	/** Sends vertices and indices, stored at `buffer`, to client */
	void sendFrameData(int64_t frameId, uint8_t *buffer, int nVertices, int nIndices);
public:
	std::chrono::milliseconds targetFrameTime;

	ServerActiveEndpoint(Server& server)
		: server{ server }
		, targetFrameTime{ std::chrono::milliseconds{ 33 } }
	{}
};

/** This class implements the passive server thread which receives client information
 *  (camera position, etc) during every frame.
 *  It will wait indefinitely on its UDP socket and send the data to the server's shared
 *  data memory as soon as possible.
 */
class ServerPassiveEndpoint : public Endpoint {
	Server& server;

	void loopFunc() override;

public:
	ServerPassiveEndpoint(Server& server) : server{ server } {}
};

/** This struct contains data that is shared between the server's active and passive endpoints. */
struct SharedServerData final {
	/** Notified whenever a new frame arrives from the client */
	std::condition_variable loopCv;

	/** The latest frame received from client */
	int64_t clientFrame = -1;

	/** Mutex guarding access to clientData */
	std::mutex clientDataMtx;

	/** Payload received from the client goes here*/
	std::array<uint8_t, FrameData().payload.size()> clientData;
};

/** The Server class wraps the active and passive endpoints and provides a mean to sharing
 *  data between the two threads.
 *  It also functions as a convenient common entrypoint for starting both threads.
 */
class Server {
	ServerActiveEndpoint activeEP;
	ServerPassiveEndpoint passiveEP;

public:
	SharedServerData shared;

	explicit Server();

	void run(const char *activeIp, int activePort, const char *passiveIp, int passivePort);
	void close();
};


/** This class implements a reliable connection server endpoint which handles the server-side
 *  reliable communication channel.
 *  It's used to perform initial handshake and as a keepalive for the client.
 */
class ServerReliableEndpoint : public Endpoint {

	void loopFunc() override;

	/** This method listens to an accepted connection coming from loopFunc.
	 *  It runs in a detached thread.
	 */
	void listenTo(socket_t clientSocket, sockaddr_in clientAddr);
	bool performHandshake(socket_t clientSocket);
};
