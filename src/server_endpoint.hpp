#pragma once

#include <mutex>
#include <condition_variable>
#include "endpoint.hpp"
#include "vertex.hpp"
#include "data.hpp"

class Server;

/** Sends geometry to client */
class ServerActiveEndpoint : public Endpoint {
	Server& server;
	uint8_t *serverMemory = nullptr;

	void loopFunc() override;
	void sendFrameData(int64_t frameId, Vertex *vertices, int nVertices, Index *indices, int nIndices);
public:
	ServerActiveEndpoint(Server& server) : server(server) {}
};

/** Receives client info (camera, etc) */
class ServerPassiveEndpoint : public Endpoint {
	Server& server;

	void loopFunc() override;

public:
	ServerPassiveEndpoint(Server& server) : server(server) {}
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

class Server {
	ServerActiveEndpoint activeEP;
	ServerPassiveEndpoint passiveEP;

public:
	SharedServerData shared;

	explicit Server();

	void run(const char *activeIp, int activePort, const char *passiveIp, int passivePort);
	void close();
};
