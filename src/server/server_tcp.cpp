#include "server_tcp.hpp"
#include "logging.hpp"
#include "server.hpp"
#include "server_resources.hpp"
#include "tcp_messages.hpp"
#include "tcp_serialize.hpp"
#include <array>

using namespace logging;

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
		for (const auto& pair : server.resources.models)
			server.toClient.modelsToSend.emplace_back(pair.second);
	}
}

static bool tcp_connectionPrelude(socket_t clientSocket)
{
	// Connection prelude (one-time stuff)

	std::array<uint8_t, 1> buffer = {};

	// Perform handshake
	if (!expectTCPMsg(clientSocket, buffer.data(), buffer.size(), TcpMsgType::HELO))
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
	if (!expectTCPMsg(clientSocket, buffer.data(), buffer.size(), TcpMsgType::READY))
		return false;

	// Starts UDP loops and send ready to client
	// server.activeEP.startActive(readableAddr, cfg::SERVER_TO_CLIENT_PORT, SOCK_DGRAM);
	// server.activeEP.runLoop();
	// server.passiveEP.startPassive(ip.c_str(), cfg::CLIENT_TO_SERVER_PORT, SOCK_DGRAM);
	// server.passiveEP.runLoop();

	if (!sendTCPMsg(clientSocket, TcpMsgType::READY))
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
		err("sendOneTimeData: failed");
		return false;
	}

	uint8_t packet;
	ok = expectTCPMsg(clientSocket, &packet, 1, TcpMsgType::RSRC_EXCHANGE_ACK);
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

	uint8_t packet;
	ok = expectTCPMsg(clientSocket, &packet, 1, TcpMsgType::RSRC_EXCHANGE_ACK);
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

	uint8_t packet;
	ok = expectTCPMsg(clientSocket, &packet, 1, TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}

	info("model.materials = ", model.materials.size());
	for (const auto& mat : model.materials) {
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

	uint8_t packet;
	ok = expectTCPMsg(clientSocket, &packet, 1, TcpMsgType::RSRC_EXCHANGE_ACK);
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

	ok = expectTCPMsg(clientSocket, &packet, 1, TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}

	return true;
}

static bool batch_sendPointLight(socket_t clientSocket, const shared::PointLight& light)
{
	uint8_t packet;
	bool ok = sendPointLight(clientSocket, light);
	if (!ok) {
		err("Failed sending point light");
		return false;
	}
	ok = expectTCPMsg(clientSocket, &packet, 1, TcpMsgType::RSRC_EXCHANGE_ACK);
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
	for (const auto model : batch.models) {
		assert(model);
		// This will also send dependent materials and textures
		if (!batch_sendModel(clientSocket, resources, materialsSent, texturesSent, *model))
			return false;
	}

	// Send lights
	for (const auto& light : batch.pointLights) {
		assert(light);
		if (!batch_sendPointLight(clientSocket, *light))
			return false;
	}

	// Send shaders (and unload them immediately after)
	const std::array<const char*, 3> shadersToSend = { "shaders/gbuffer", "shaders/skybox", "shaders/composition" };
	for (unsigned i = 0; i < shadersToSend.size(); ++i) {
		if (!batch_sendShaders(clientSocket, resources, shadersToSend[i], i))
			return false;
	}

	info("Done sending data");

	return true;
}

///////////////////

TcpActiveThread::TcpActiveThread(Server& server, Endpoint& ep)
	: server{ server }
	, ep{ ep }
{
	thread = std::thread{ &TcpActiveThread::tcpActiveTask, this };
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
	::listen(ep.socket, 1);

	while (ep.connected) {
		sockaddr_in clientAddr;
		socklen_t clientLen = sizeof(sockaddr_in);

		info("Accepting...");
		auto clientSocket = ::accept(ep.socket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
		if (clientSocket == -1) {
			if (ep.connected)
				err("Error: couldn't accept connection.");
			continue;
		}

		info("Accepted connection from ", inet_ntoa(clientAddr.sin_addr));

		genUpdateLists(server);

		if (!tcp_connectionPrelude(clientSocket))
			dropClient(clientSocket);

		if (!msgLoop(clientSocket, clientAddr)) {
			const char* readableAddr = inet_ntoa(clientAddr.sin_addr);
			info("TCP: Dropping client ", readableAddr);
			dropClient(clientSocket);
		}
	}
}

bool TcpActiveThread::msgLoop(socket_t clientSocket, sockaddr_in clientAddr)
{
	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds{ 1000000 });
		break;
	}

	return false;
}

void TcpActiveThread::dropClient(socket_t clientSocket)
{
	// Send disconnect message
	sendTCPMsg(clientSocket, TcpMsgType::DISCONNECT);
	info("Closing passiveEP");
	closeEndpoint(server.endpoints.udpPassive);
	info("Closing activeEP");
	closeEndpoint(server.endpoints.udpActive);
}
///////////

// KeepaliveListenThread::KeepaliveListenThread(socket_t clientSocket)
//: clientSocket{ clientSocket }
//{
// thread = std::thread{ &KeepaliveListenThread::keepaliveListenTask, this };
//}

// KeepaliveListenThread::~KeepaliveListenThread()
//{
// if (thread.joinable()) {
// info("Joining keepaliveThread...");
// thread.join();
// info("Joined keepaliveThread.");
//}
//}

//[>* This task listens for keepalives and updates `latestPing` with the current time every time it receives one. <]
// void KeepaliveListenThread::keepaliveListenTask()
//{
// std::array<uint8_t, 1> buffer = {};

// while (true) {
// TcpMsgType type;
// if (!receiveTCPMsg(clientSocket, buffer.data(), buffer.size(), type)) {
// break;
//}

// switch (type) {
// case TcpMsgType::KEEPALIVE:
// latestPing = std::chrono::system_clock::now();
// break;
// case TcpMsgType::DISCONNECT:
// goto exit;
// default:
// break;
//}
//}
// exit:
// info("KEEPALIVE: dead");
// cv.notify_one();
// return;
//}

/*
		std::unique_lock<std::mutex> keepaliveUlk{ keepaliveMtx };
		// TODO: ensure no spurious wakeup
		if (keepaliveCv.wait_for(keepaliveUlk, interval) == std::cv_status::no_timeout) {
			info("Keepalive thread should be dead.");
			break;
		}

		// Verify the client has pinged us more recently than
		const auto now = std::chrono::system_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(now - roLatestPing) > interval) {
			// drop the client
			info("Keepalive timeout.");
			break;
		}
		*/
