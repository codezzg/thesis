#pragma once

#include "cf_hashmap.hpp"
#include "cf_hashset.hpp"
#include "queued_update.hpp"
#include "server_resources.hpp"
#include "server_tcp.hpp"
#include "server_udp.hpp"
#include "spatial.hpp"
#include "udp_messages.hpp"
#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

struct ClientToServerData {
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

namespace std {
template <>
struct hash<std::pair<std::string, shared::TextureFormat>> {
	std::size_t operator()(const std::pair<std::string, shared::TextureFormat>& p) const
	{
		return std::hash<std::string>{}(p.first) ^ (std::hash<uint8_t>{}(static_cast<uint8_t>(p.second)) << 1);
	}
};
}   // namespace std

using TexturesQueue = std::unordered_set<std::pair<std::string, shared::TextureFormat>>;

struct ServerToClientData {
	/** List of queued UDP updates to send to the client */
	UpdateList updates;

	/** List of models whose geometry still needs to be sent to client */
	std::vector<Model> modelsToSend;
	std::mutex modelsToSendMtx;

	/** Map { modelName => [textures] } storing the textures to send after all
	 *  model geometry has been received by the client.
	 */
	TexturesQueue texturesQueue;
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
		std::unique_ptr<TcpReceiveThread> tcpRecv;
	} networkThreads;

	std::string cwd;

	ClientToServerData fromClient;
	ServerToClientData toClient;

	ServerResources resources;
	Scene scene;
	/** Keeps track of resources sent to the client */
	cf::hashset<StringId> stuffSent;

	/** Constructs a Server with `memsize` internal memory. */
	explicit Server(std::size_t memsize);
	~Server();

	void closeNetwork();
};

/** Loads model `name` into `server`'s resources. */
bool loadSingleModel(Server& server, std::string name, Model* outModel = nullptr);
