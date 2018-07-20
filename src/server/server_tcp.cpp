#include "server_tcp.hpp"
#include "blocking_queue.hpp"
#include "logging.hpp"
#include "server.hpp"
#include "server_resources.hpp"
#include "tcp_messages.hpp"
#include "tcp_serialize.hpp"
#include "xplatform.hpp"
#include <array>
#include <chrono>

using namespace logging;

struct TcpMsg {
	TcpMsgType type;
	/** Currently only used by REQ_MODEL */
	uint16_t payload;
};

static BlockingQueue<TcpMsg> gMsgRecvQueue;
static std::chrono::time_point<std::chrono::steady_clock> gLatestPing;

static void genUpdateLists(Server& server)
{
	// Regenerate lists of stuff to send
	{
		std::lock_guard<std::mutex> lock{ server.toClient.updates.mtx };
		server.toClient.updates.persistent.clear();
	}
	{
		// Build the initial list of models to send to the client
		std::lock_guard<std::mutex> lock{ server.toClient.modelsToSendMtx };
		server.toClient.modelsToSend.reserve(server.resources.models.size());
		auto it = server.resources.models.iter_start();
		StringId ignore;
		Model model;
		while (server.resources.models.iter_next(it, ignore, model))
			server.toClient.modelsToSend.emplace_back(model);
	}
}

static void loadAndEnqueueModel(Server& server, unsigned n)
{
	static const std::array<const char*, 4> modelList = { "/models/sponza/sponza.dae",
		"/models/nanosuit/nanosuit.obj",
		"/models/cat/cat.obj",
		"/models/wall/wall2.obj" };

	info("loadAndSendModel(", n, ")");

	if (n >= modelList.size()) {
		warn("Received a REQ_MODEL (", n, "), but models are only ", modelList.size(), "!");
		return;
	}

	Model model;
	if (!loadSingleModel(server, modelList[n], &model))
		return;

	// Note: tcpActive->mtx is already locked by us
	server.networkThreads.tcpActive->resourcesToSend.models.emplace(model);

	{
		std::lock_guard<std::mutex> lock{ server.toClient.modelsToSendMtx };
		server.toClient.modelsToSend.emplace_back(model);
	}

	auto node = server.scene.addNode(model.name, NodeType::MODEL, Transform{});
	// Make Sponza static
	if (n == 0)
		node->flags |= (1 << NODE_FLAG_STATIC);
}

static bool expectTCPMsg(TcpMsgType type)
{
	return gMsgRecvQueue.pop_or_wait().type == type;
}

static bool tcp_connectionPrelude(socket_t clientSocket)
{
	// Connection prelude (one-time stuff)

	// Perform handshake
	if (!expectTCPMsg(TcpMsgType::HELO))
		return false;

	if (!sendTCPMsg(clientSocket, TcpMsgType::HELO_ACK))
		return false;

	// Send one-time data
	// info("Sending one time data...");
	// if (!sendTCPMsg(clientSocket, TcpMsgType::START_RSRC_EXCHANGE))
	// return false;

	// if (!expectTCPMsg(clientSocket, buffer.data(), buffer.size(), TcpMsgType::RSRC_EXCHANGE_ACK))
	// return false;

	// if (!sendOneTimeData(clientSocket))
	// return false;

	// if (!sendTCPMsg(clientSocket, TcpMsgType::END_RSRC_EXCHANGE))
	// return false;

	// Wait for ready signal from client
	if (!expectTCPMsg(TcpMsgType::READY))
		return false;

	return true;
}

static bool batch_sendTexture(socket_t clientSocket,
	ServerResources& resources,
	/* inout */ std::unordered_set<std::string>& texturesSent,
	const std::string& texName,
	shared::TextureFormat fmt)
{
	if (texName.length() == 0 || texturesSent.count(texName) > 0)
		return true;

	info("* sending texture ", texName);

	bool ok = sendTexture(clientSocket, resources, texName, fmt);
	if (!ok) {
		err("batch_sendTexture: failed");
		return false;
	}

	ok = expectTCPMsg(TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}

	texturesSent.emplace(texName);

	return true;
}

/** Send material (along with textures used by it) */
static bool batch_sendMaterial(socket_t clientSocket,
	ServerResources& resources,
	/* inout */ std::unordered_set<StringId>& materialsSent,
	/* inout */ std::unordered_set<std::string>& texturesSent,
	const Material& mat)
{
	// Don't send the same material twice
	if (materialsSent.count(mat.name) != 0)
		return true;

	debug("sending new material ", mat.name);

	bool ok = sendMaterial(clientSocket, mat);
	if (!ok) {
		err("Failed sending material");
		return false;
	}

	ok = expectTCPMsg(TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}
	materialsSent.emplace(mat.name);

	if (!batch_sendTexture(clientSocket, resources, texturesSent, mat.diffuseTex, shared::TextureFormat::RGBA))
		return false;
	if (!batch_sendTexture(clientSocket, resources, texturesSent, mat.specularTex, shared::TextureFormat::GREY))
		return false;
	if (!batch_sendTexture(clientSocket, resources, texturesSent, mat.normalTex, shared::TextureFormat::RGBA))
		return false;

	return true;
}

/** Send model (along with materials used by it) */
static bool batch_sendModel(socket_t clientSocket,
	ServerResources& resources,
	/* inout */ std::unordered_set<StringId>& materialsSent,
	/* inout */ std::unordered_set<std::string>& texturesSent,
	const Model& model)
{
	bool ok = sendModel(clientSocket, model);
	if (!ok) {
		err("Failed sending model");
		return false;
	}

	ok = expectTCPMsg(TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}

	info("model.materials = ", model.data->materials.size());
	for (const auto& mat : model.data->materials) {
		if (!batch_sendMaterial(clientSocket, resources, materialsSent, texturesSent, mat))
			return false;
	}

	return true;
}

static bool
	batch_sendShaders(socket_t clientSocket, ServerResources& resources, const char* baseName, uint8_t shaderStage)
{
	bool ok = sendShader(clientSocket,
		resources,
		(std::string{ baseName } + ".vert.spv").c_str(),
		shaderStage,
		shared::ShaderStage::VERTEX);
	if (!ok) {
		err("Failed sending shader");
		return false;
	}

	ok = expectTCPMsg(TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}

	ok = sendShader(clientSocket,
		resources,
		(std::string{ baseName } + ".frag.spv").c_str(),
		shaderStage,
		shared::ShaderStage::FRAGMENT);
	if (!ok) {
		err("Failed sending shader");
		return false;
	}

	ok = expectTCPMsg(TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}

	return true;
}

static bool batch_sendPointLight(socket_t clientSocket, const shared::PointLight& light)
{
	bool ok = sendPointLight(clientSocket, light);
	if (!ok) {
		err("Failed sending point light");
		return false;
	}
	ok = expectTCPMsg(TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}
	return true;
}

static bool sendResourceBatch(socket_t clientSocket, ServerResources& resources, const ResourceBatch& batch)
{
	std::unordered_set<std::string> texturesSent;
	std::unordered_set<StringId> materialsSent;

	info("Sending ", batch.models.size(), " models");
	for (const auto& model : batch.models) {
		// This will also send dependent materials and textures
		if (!batch_sendModel(clientSocket, resources, materialsSent, texturesSent, model))
			return false;
	}

	// Send lights
	for (const auto& light : batch.pointLights) {
		if (!batch_sendPointLight(clientSocket, light))
			return false;
	}

	// Send shaders (and unload them immediately after)
	// const std::array<const char*, 3> shadersToSend = { "shaders/gbuffer", "shaders/skybox",
	// "shaders/composition"
	// }; for (unsigned i = 0; i < shadersToSend.size(); ++i) { if (!batch_sendShaders(clientSocket,
	// resources, shadersToSend[i], i)) return false;
	//}

	if (!sendTCPMsg(clientSocket, TcpMsgType::END_RSRC_EXCHANGE))
		return false;

	info("Done sending data");

	return true;
}

///////////////////

TcpActiveThread::TcpActiveThread(Server& server, Endpoint& ep)
	: server{ server }
	, ep{ ep }
{
	thread = std::thread{ &TcpActiveThread::tcpActiveTask, this };
	xplatSetThreadName(thread, "TcpActive");
}

TcpActiveThread::~TcpActiveThread()
{
	if (thread.joinable()) {
		info("Joining Tcp Active thread...");
		thread.join();
		info("Joined Tcp Active thread.");
	}
}

void TcpActiveThread::tcpActiveTask()
{
	info("Listening...");
	// One client at a time
	if (::listen(ep.socket, 1) != 0) {
		err("Error listening: ", xplatGetErrorString(), " (", xplatGetError(), ")");
		return;
	}

	while (ep.connected) {
		sockaddr_in clientAddr;
		socklen_t clientLen = sizeof(sockaddr_in);

		info("Accepting...");
		auto clientSocket = ::accept(ep.socket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
		if (clientSocket == -1) {
			if (ep.connected) {
				err("Error: couldn't accept connection.");
				closeEndpoint(ep);
				break;
			}
			continue;
		}

		info("Accepted connection from ", inet_ntoa(clientAddr.sin_addr));

		// Start receiving thread
		server.networkThreads.tcpRecv =
			std::make_unique<TcpReceiveThread>(server, server.endpoints.reliable, clientSocket);

		// genUpdateLists(server);

		if (!tcp_connectionPrelude(clientSocket))
			dropClient(clientSocket);

		const char* readableAddr = inet_ntoa(clientAddr.sin_addr);

		connectToClient(clientSocket, readableAddr);

		if (!sendTCPMsg(clientSocket, TcpMsgType::READY)) {
			info("TCP: Dropping client ", readableAddr);
			dropClient(clientSocket);
		}

		if (!msgLoop(clientSocket)) {
			info("TCP: Dropping client ", readableAddr);
			dropClient(clientSocket);
		}
	}

	info("tcpActiveTask: ended.");
}

void TcpActiveThread::connectToClient(socket_t clientSocket, const char* clientAddr)
{
	// Start keepalive listening thread
	server.networkThreads.keepalive =
		std::make_unique<KeepaliveListenThread>(server, server.endpoints.reliable, clientSocket);

	// Starts UDP loops and send ready to client
	server.endpoints.udpActive =
		startEndpoint(clientAddr, cfg::UDP_SERVER_TO_CLIENT_PORT, Endpoint::Type::ACTIVE, SOCK_DGRAM);
	server.networkThreads.udpActive = std::make_unique<UdpActiveThread>(server, server.endpoints.udpActive);
	server.endpoints.udpPassive =
		startEndpoint(ep.ip.c_str(), cfg::UDP_CLIENT_TO_SERVER_PORT, Endpoint::Type::PASSIVE, SOCK_DGRAM);
	server.networkThreads.udpPassive = std::make_unique<UdpPassiveThread>(server, server.endpoints.udpPassive);
}

bool TcpActiveThread::msgLoop(socket_t clientSocket)
{
	while (ep.connected) {
		std::unique_lock<std::mutex> ulk{ mtx };
		cv.wait(ulk, [this]() {
			return !ep.connected || !server.networkThreads.tcpRecv->clientConnected ||
			       !server.networkThreads.keepalive->clientConnected || resourcesToSend.size() > 0 ||
			       gMsgRecvQueue.size() > 0;
		});

		if (!ep.connected || !server.networkThreads.keepalive->clientConnected ||
			!server.networkThreads.tcpRecv->clientConnected)
			return false;

		TcpMsg msg;
		while (gMsgRecvQueue.try_pop(msg)) {
			if (msg.type != TcpMsgType::REQ_MODEL)
				continue;

			loadAndEnqueueModel(server, msg.payload);
		}

		if (resourcesToSend.size() > 0) {
			if (!sendTCPMsg(clientSocket, TcpMsgType::START_RSRC_EXCHANGE))
				return false;

			if (!expectTCPMsg(TcpMsgType::RSRC_EXCHANGE_ACK))
				return false;

			info("Send ResourceBatch");
			if (!sendResourceBatch(clientSocket, server.resources, resourcesToSend))
				return false;

			resourcesToSend.clear();
		}
	}

	return false;
}

void TcpActiveThread::dropClient(socket_t clientSocket)
{
	info("Dropping client");

	// Send disconnect message
	sendTCPMsg(clientSocket, TcpMsgType::DISCONNECT);

	info("Closing passiveEP");
	closeEndpoint(server.endpoints.udpPassive);
	server.networkThreads.udpPassive.reset(nullptr);

	info("Closing activeEP");
	closeEndpoint(server.endpoints.udpActive);
	server.networkThreads.udpActive.reset(nullptr);

	server.networkThreads.tcpRecv->clientConnected = false;
	server.networkThreads.keepalive.reset(nullptr);

	xplatSockClose(clientSocket);
	server.networkThreads.tcpRecv->clientConnected = false;
	server.networkThreads.tcpRecv.reset(nullptr);
}
///////////

KeepaliveListenThread::KeepaliveListenThread(Server& server, const Endpoint& ep, socket_t clientSocket)
	: ServerSlaveThread{ server, ep, clientSocket }
{
	thread = std::thread{ &KeepaliveListenThread::keepaliveListenTask, this };
	xplatSetThreadName(thread, "KeepaliveListen");
}

KeepaliveListenThread::~KeepaliveListenThread()
{
	if (thread.joinable()) {
		info("Joining keepaliveThread...");
		cv.notify_all();
		thread.join();
		info("Joined keepaliveThread.");
	}
}

/* This task listens for keepalives and updates `latestPing` with the current time every time it receives one.
 */
void KeepaliveListenThread::keepaliveListenTask()
{
	constexpr auto interval = std::chrono::seconds{ cfg::SERVER_KEEPALIVE_INTERVAL_SECONDS };

	while (ep.connected && clientConnected) {
		std::unique_lock<std::mutex> ulk{ mtx };
		if (cv.wait_for(ulk, interval) == std::cv_status::no_timeout) {
			break;
		}

		// Verify the client has pinged us within our sleep time
		const auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(now - gLatestPing) > interval) {
			// drop the client
			info("Keepalive timeout.");
			break;
		}
	}
	clientConnected = false;
	if (server.networkThreads.tcpActive)
		server.networkThreads.tcpActive->cv.notify_one();
}

///////////////
TcpReceiveThread::TcpReceiveThread(Server& server, const Endpoint& ep, socket_t clientSocket)
	: ServerSlaveThread{ server, ep, clientSocket }
{
	thread = std::thread{ &TcpReceiveThread::receiveTask, this };
	xplatSetThreadName(thread, "TcpReceive");
}

TcpReceiveThread::~TcpReceiveThread()
{
	if (thread.joinable()) {
		info("Joining ReceiveThread...");
		thread.join();
		info("Joined ReceiveThread.");
	}
}

void TcpReceiveThread::receiveTask()
{
	info("Started receiveTask");

	if (gMsgRecvQueue.capacity() == 0)
		gMsgRecvQueue.reserve(256);
	else
		gMsgRecvQueue.clear();

	int failCount = 0;
	constexpr int MAX_FAIL_COUNT = 10;

	while (ep.connected && clientConnected) {
		std::array<uint8_t, 3> packet;
		packet.fill(0);
		TcpMsgType type;
		if (receiveTCPMsg(clientSocket, packet.data(), packet.size(), type)) {
			failCount = 0;
			switch (type) {
			case TcpMsgType::DISCONNECT:
				info("Received DISCONNECT from client.");
				goto exit;
			case TcpMsgType::KEEPALIVE:
				gLatestPing = std::chrono::steady_clock::now();
				break;
			default: {
				info("pushing msg ", type);
				TcpMsg msg;
				msg.type = type;
				if (type == TcpMsgType::REQ_MODEL) {
					msg.payload = *reinterpret_cast<uint16_t*>(packet.data() + 1);
				}
				gMsgRecvQueue.push(msg);
				server.networkThreads.tcpActive->cv.notify_one();
			} break;
			}
		} else {
			if (++failCount == MAX_FAIL_COUNT)
				break;
		}
	}
exit:
	clientConnected = false;
	if (server.networkThreads.tcpActive)
		server.networkThreads.tcpActive->cv.notify_one();
}

