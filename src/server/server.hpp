#pragma once

#include "queued_update.hpp"
#include "server_endpoint.hpp"
#include "server_resources.hpp"
#include "spatial.hpp"
#include "udp_messages.hpp"
#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

struct ClientToServerData {
	/** The latest frame received from client */
	int64_t clientFrame = -1;

	/** Mutex guarding access to clientData */
	std::mutex clientDataMtx;

	/** Notified whenever a new frame arrives from the client */
	std::condition_variable clientDataCv;

	/** Payload received from the client goes here*/
	std::array<uint8_t, FrameData().payload.size()> clientData;
};

struct ServerToClientData {
	/** List of queued UDP updates to send to the client */
	std::vector<std::unique_ptr<QueuedUpdate>> updates;

	/** Mutex guarding updates */
	std::mutex updatesMtx;

	/** Notified whenever there are updates to send to the client */
	std::condition_variable updatesCv;
};

/** The Server wraps the endpoints and provides a mean to sharing data between the server threads.
 *  It also functions as a convenient common entrypoint for starting and terminating threads.
 */
struct Server {
	std::vector<uint8_t> memory;

	ServerActiveEndpoint activeEP;
	ServerPassiveEndpoint passiveEP;
	ServerReliableEndpoint relEP;

	ClientToServerData fromClient;
	ServerToClientData toClient;

	ServerResources resources;
	Scene scene;

	/** Constructs a Server with `memsize` internal memory. */
	explicit Server(std::size_t memsize);
	~Server();

	void closeNetwork();
};
