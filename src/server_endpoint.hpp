#pragma once

#include "endpoint.hpp"

class Server;

/** Sends geometry to client */
class ServerActiveEndpoint : public Endpoint {
	Server& server;

	void loopFunc() override;
public:
	ServerActiveEndpoint(Server& server) : server(server) {}
};

/** Receives client info (camera, etc) */
class ServerPassiveEndpoint : public Endpoint {
	Server& server;
	uint8_t *buffer = nullptr;

	void loopFunc() override;

public:
	ServerPassiveEndpoint(Server& server) : server(server) {}
};

class Server {
	ServerActiveEndpoint activeEP;
	ServerPassiveEndpoint passiveEP;

public:
	explicit Server() : activeEP(*this), passiveEP(*this) {}

	void run(const char *activeIp, int activePort, const char *passiveIp, int passivePort);
	void close();
};
