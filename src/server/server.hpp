#pragma once

#include "server_endpoint.hpp"
#include "server_resources.hpp"
#include "udp_messages.hpp"
#include <array>
#include <condition_variable>
#include <mutex>
#include <vector>

/** This struct contains data that is shared between the server's active and passive endpoints. */
struct ServerSharedData final {
	//// Client per-frame data

	/** The latest frame received from client */
	int64_t clientFrame = -1;

	/** Mutex guarding access to clientData */
	std::mutex clientDataMtx;

	/** Notified whenever a new frame arrives from the client */
	std::condition_variable clientDataCv;

	/** Payload received from the client goes here*/
	std::array<uint8_t, FrameData().payload.size()> clientData;

	//// Geometry updating

	/** List of update chunks to send */
	std::vector<udp::ChunkHeader> geomUpdate;

	/** Mutex guarding geomUpdate */
	std::mutex geomUpdateMtx;

	/** Notified whenever there are geometry updates to send to the client*/
	std::condition_variable geomUpdateCv;
};

/** The Server wraps the endpoints and provides a mean to sharing data between the server threads.
 *  It also functions as a convenient common entrypoint for starting and terminating threads.
 */
struct Server final {
	std::vector<uint8_t> memory;

	ServerActiveEndpoint activeEP;
	ServerPassiveEndpoint passiveEP;
	ServerReliableEndpoint relEP;
	ServerSharedData shared;

	ServerResources resources;

	explicit Server(std::size_t memsize);
	~Server();

	void closeNetwork();
};
