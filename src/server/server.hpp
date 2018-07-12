#pragma once

#include "cf_hashmap.hpp"
#include "queued_update.hpp"
#include "server_endpoint.hpp"
#include "server_resources.hpp"
#include "server_tcp.hpp"
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

	std::vector<uint32_t> acksReceived;
	std::mutex acksReceivedMtx;
};

struct UpdateList {
	/** Updates in this list get wiped out every appstage loop */
	std::vector<QueuedUpdate> transitory;
	/** Updates in this list must be ACKed by the client before they get deleted. */
	cf::hashmap<uint32_t, QueuedUpdate> persistent;

	/** Mutex guarding updates */
	std::mutex mtx;

	/** Notified whenever there are updates to send to the client */
	std::condition_variable cv;

	std::size_t size() const { return transitory.size() + persistent.size(); }
};

struct ServerToClientData {
	/** List of queued UDP updates to send to the client */
	UpdateList updates;

	/** List of models whose geometry still needs to be sent to client */
	std::vector<Model> modelsToSend;
	std::mutex modelsToSendMtx;
};

/** The Server wraps the endpoints and provides a mean to sharing data between the server threads.
 *  It also functions as a convenient common entrypoint for starting and terminating threads.
 */
struct Server {
	std::vector<uint8_t> memory;
	StackAllocator allocator;

	struct {
		Endpoint udpActive;
		Endpoint udpPassive;
		Endpoint reliable;
	} endpoints;

	struct {
		std::unique_ptr<UdpActiveThread> udpActive;
		std::unique_ptr<UdpPassiveThread> udpPassive;
		std::unique_ptr<TcpActiveThread> tcpActive;
		std::unique_ptr<KeepaliveListenThread> keepalive;
	} networkThreads;

	ClientToServerData fromClient;
	ServerToClientData toClient;

	ServerResources resources;
	Scene scene;

	/** Constructs a Server with `memsize` internal memory. */
	explicit Server(std::size_t memsize);
	~Server();

	void closeNetwork();
};
