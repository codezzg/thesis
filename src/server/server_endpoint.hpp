#pragma once

#include "endpoint.hpp"
#include "ext_mem_user.hpp"
#include "frame_data.hpp"
#include "hashing.hpp"
#include "shared_resources.hpp"
#include "vertex.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>

struct Server;
struct ServerSharedData;

/** This class implements the active server thread which sends geometry to client
 *  via an UDP socket.
 *  It will send data at regular fixed intervals determined by `targetFrameTime`.
 */
class ServerActiveEndpoint final
	: public Endpoint
	, public ExternalMemoryUser {

	Server& server;

	void loopFunc() override;
	void onClose() override;

public:
	std::chrono::milliseconds targetFrameTime;

	/** Constructs a ServerActiveEndpoint owned by `server`.
	 */
	explicit ServerActiveEndpoint(Server& server)
		: server{ server }
		, targetFrameTime{ std::chrono::milliseconds{ 33 } }
	{}
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

	std::mutex keepaliveMtx;
	/** Used to wait in the keepalive listen loop */
	std::condition_variable keepaliveCv;

	std::thread keepaliveThread;

	void loopFunc() override;

	/** This method listens to an accepted connection coming from loopFunc.
	 *  It runs in a detached thread.
	 */
	void listenTo(socket_t clientSocket, sockaddr_in clientAddr);
	void onClose() override;

	/** Sends all the one-time data the client needs. */
	bool sendOneTimeData(socket_t clientSocket);

public:
	std::string serverIp;

	explicit ServerReliableEndpoint(Server& server)
		: server{ server }
	{}
};
