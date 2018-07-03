#include "server_endpoint.hpp"
#include "camera.hpp"
#include "clock.hpp"
#include "config.hpp"
#include "defer.hpp"
#include "fps_counter.hpp"
#include "frame_data.hpp"
#include "frame_utils.hpp"
#include "geom_update.hpp"
#include "logging.hpp"
#include "model.hpp"
#include "server.hpp"
#include "server_appstage.hpp"
#include "tcp_messages.hpp"
#include "tcp_serialize.hpp"
#include "udp_messages.hpp"
#include "udp_serialize.hpp"
#include "utils.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <glm/gtx/string_cast.hpp>
#include <iostream>
#include <thread>
#include <vector>

using namespace logging;
using namespace std::chrono_literals;

void ServerActiveEndpoint::loopFunc()
{
	uint64_t packetGen = 0;

	std::array<uint8_t, cfg::PACKET_SIZE_BYTES> buffer = {};

	// Send datagrams to the client
	while (!terminated) {

		std::unique_lock<std::mutex> ulk{ server.toClient.updatesMtx };
		if (server.toClient.updates.size() == 0) {
			// Wait for updates
			server.toClient.updatesCv.wait(
				ulk, [this]() { return terminated || server.toClient.updates.size() > 0; });
		}

		auto offset = writeUdpHeader(buffer.data(), buffer.size(), packetGen);
		verbose("updates.size now = ", server.toClient.updates.size());

		unsigned i = 0;
		auto w = server.toClient.updates.begin();
		for (auto it = w; it != server.toClient.updates.end(); ++it) {
			const auto& update = *it;
			const auto written = addUpdate(buffer.data(), buffer.size(), offset, update, server);
			if (written > 0) {
				offset += written;
				++i;
			} else {
				// Not enough room: send the packet and go on
				if (gDebugLv >= LOGLV_VERBOSE) {
					dumpFullPacket(buffer.data(), buffer.size(), LOGLV_VERBOSE);
					dumpBytesIntoFileBin(
						(std::string{ "dumps/server_packet" } + std::to_string(i - 1) + ".data")
							.c_str(),
						buffer.data(),
						buffer.size());
				}
				sendPacket(socket, buffer.data(), buffer.size());
				writeUdpHeader(buffer.data(), buffer.size(), packetGen);
				offset = sizeof(UdpHeader);
				if (it != w)
					*w = std::move(*it);
				++w;
			}
		}
		assert(ulk.owns_lock());
		server.toClient.updates.erase(w, server.toClient.updates.end());

		if (offset > sizeof(UdpHeader)) {
			// Need to send the last packet
			if (gDebugLv >= LOGLV_VERBOSE) {
				dumpFullPacket(buffer.data(), buffer.size(), LOGLV_VERBOSE);
				dumpBytesIntoFileBin(
					(std::string{ "dumps/server_packet" } + std::to_string(i - 1) + ".data")
						.c_str(),
					buffer.data(),
					buffer.size());
			}
			sendPacket(socket, buffer.data(), buffer.size());
		}

		++packetGen;
	}
}

////////////////////////////////////////

// Receives client parameters wherewith the server shall calculate the primitives to send during next frame
void ServerPassiveEndpoint::loopFunc()
{
	// Track the latest frame we received
	int64_t latestFrame = -1;
	int nPacketRecvErrs = 0;

	while (!terminated) {
		std::array<uint8_t, sizeof(FrameData)> packetBuf = {};
		if (!receivePacket(socket, packetBuf.data(), packetBuf.size())) {
			if (++nPacketRecvErrs > 10)
				break;
			else
				continue;
		}
		nPacketRecvErrs = 0;

		if (!validateUDPPacket(packetBuf.data(), latestFrame))
			continue;

		const auto packet = reinterpret_cast<FrameData*>(packetBuf.data());
		verbose("Received packet ", packet->header.frameId);
		if (packet->header.frameId <= latestFrame)
			continue;

		latestFrame = packet->header.frameId;
		{
			// Update shared data
			std::lock_guard<std::mutex> lock{ server.fromClient.clientDataMtx };
			memcpy(server.fromClient.clientData.data(), packet->payload.data(), packet->payload.size());
			server.fromClient.clientFrame = latestFrame;
		}
		server.fromClient.clientDataCv.notify_one();
	}
}

/////////////////////////////////////////

void ServerReliableEndpoint::loopFunc()
{
	// FIXME?
	constexpr auto MAX_CLIENTS = 1;

	info("Listening...");
	::listen(socket, MAX_CLIENTS);

	while (!terminated) {
		sockaddr_in clientAddr;
		socklen_t clientLen = sizeof(sockaddr_in);

		info("Accepting...");
		auto clientSocket = ::accept(socket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
		if (clientSocket == -1) {
			err("Error: couldn't accept connection.");
			continue;
		}

		info("Accepted connection from ", inet_ntoa(clientAddr.sin_addr));
		// For concurrent client handling, uncomment this and comment `listenTo`
		// std::thread listener(&ServerReliableEndpoint::listenTo, this, clientSocket, clientAddr);
		// listener.detach();

		// Single client
		listenTo(clientSocket, clientAddr);
	}
}

/** This task listens for keepalives and updates `latestPing` with the current time every time it receives one. */
static void keepaliveTask(socket_t clientSocket,
	std::condition_variable& cv,
	std::chrono::time_point<std::chrono::system_clock>& latestPing)
{
	std::array<uint8_t, 1> buffer = {};

	while (true) {
		TcpMsgType type;
		if (!receiveTCPMsg(clientSocket, buffer.data(), buffer.size(), type)) {
			cv.notify_one();
			break;
		}

		switch (type) {
		case TcpMsgType::KEEPALIVE:
			latestPing = std::chrono::system_clock::now();
			break;
		case TcpMsgType::DISCONNECT:
			// Special value used to signal disconnection
			latestPing = std::chrono::time_point<std::chrono::system_clock>::max();
			break;
		default:
			break;
		}
	}
}

void ServerReliableEndpoint::listenTo(socket_t clientSocket, sockaddr_in clientAddr)
{
	const auto readableAddr = inet_ntoa(clientAddr.sin_addr);
	{
		// Connection prelude (one-time stuff)

		std::array<uint8_t, 1> buffer = {};

		// Perform handshake
		if (!expectTCPMsg(clientSocket, buffer.data(), buffer.size(), TcpMsgType::HELO))
			goto dropclient;

		if (!sendTCPMsg(clientSocket, TcpMsgType::HELO_ACK))
			goto dropclient;

		// Send one-time data
		info("Sending one time data...");
		if (!sendTCPMsg(clientSocket, TcpMsgType::START_RSRC_EXCHANGE))
			goto dropclient;

		if (!expectTCPMsg(clientSocket, buffer.data(), buffer.size(), TcpMsgType::RSRC_EXCHANGE_ACK))
			goto dropclient;

		if (!sendOneTimeData(clientSocket))
			goto dropclient;

		if (!sendTCPMsg(clientSocket, TcpMsgType::END_RSRC_EXCHANGE))
			goto dropclient;

		// Wait for ready signal from client
		if (!expectTCPMsg(clientSocket, buffer.data(), buffer.size(), TcpMsgType::READY))
			goto dropclient;

		// Starts UDP loops and send ready to client
		server.activeEP.startActive(readableAddr, cfg::SERVER_TO_CLIENT_PORT, SOCK_DGRAM);
		server.activeEP.runLoop();
		// server.passiveEP.startPassive(ip.c_str(), cfg::CLIENT_TO_SERVER_PORT, SOCK_DGRAM);
		// server.passiveEP.runLoop();

		if (!sendTCPMsg(clientSocket, TcpMsgType::READY))
			goto dropclient;
	}

	{
		// Periodically check keepalive, or drop the client
		std::chrono::time_point<std::chrono::system_clock> latestPing;
		std::thread keepaliveThread{ keepaliveTask, clientSocket, std::ref(keepaliveCv), std::ref(latestPing) };

		const auto& roLatestPing = latestPing;
		const auto interval = std::chrono::seconds{ cfg::SERVER_KEEPALIVE_INTERVAL_SECONDS };

		while (true) {
			{
				std::unique_lock<std::mutex> keepaliveUlk{ keepaliveMtx };
				// TODO: ensure no spurious wakeup
				if (keepaliveCv.wait_for(keepaliveUlk, interval) == std::cv_status::no_timeout)
					break;
			}

			// Check for disconnection
			if (roLatestPing == std::chrono::time_point<std::chrono::system_clock>::max()) {
				info("Client disconnected.");
				break;
			}

			// Verify the client has pinged us more recently than SERVER_KEEPALIVE_INTERVAL_SECONDS
			const auto now = std::chrono::system_clock::now();
			if (std::chrono::duration_cast<std::chrono::seconds>(now - roLatestPing) > interval) {
				// drop the client
				info("Keepalive timeout.");
				break;
			}
		}
		if (keepaliveThread.joinable())
			keepaliveThread.join();
	}

dropclient:
	info("TCP: Dropping client ", readableAddr);
	{
		// Send disconnect message
		sendTCPMsg(clientSocket, TcpMsgType::DISCONNECT);
	}
	server.fromClient.clientDataCv.notify_all();
	server.toClient.updatesCv.notify_all();
	// server.passiveEP.close();
	info("Closing activeEP");
	server.activeEP.close();
	info("Closing socket");
	xplatSockClose(clientSocket);
}

void ServerReliableEndpoint::onClose()
{
	keepaliveCv.notify_all();
}

bool ServerReliableEndpoint::sendOneTimeData(socket_t clientSocket)
{
	using shared::TextureFormat;

	std::array<uint8_t, 1> packet = {};
	std::unordered_set<std::string> texturesSent;
	std::unordered_set<StringId> materialsSent;

	const auto trySendTexture = [&](const std::string& texName, TextureFormat fmt = TextureFormat::RGBA) {
		if (texName.length() > 0 && texturesSent.count(texName) == 0) {
			info("* sending texture ", texName);

			bool ok = sendTexture(clientSocket, server.resources, texName, fmt);
			if (!ok) {
				err("sendOneTimeData: failed");
				return false;
			}

			ok = expectTCPMsg(clientSocket, packet.data(), 1, TcpMsgType::RSRC_EXCHANGE_ACK);
			if (!ok) {
				warn("Not received RSRC_EXCHANGE_ACK!");
				return false;
			}

			texturesSent.emplace(texName);
		}
		return true;
	};

	// Send models (and with them, textures and materials)
	info("# models loaded = ", server.resources.models.size());
	for (const auto& modpair : server.resources.models) {

		const auto& model = modpair.second;

		bool ok = sendModel(clientSocket, model);
		if (!ok) {
			err("Failed sending model");
			return false;
		}
		ok = expectTCPMsg(clientSocket, packet.data(), 1, TcpMsgType::RSRC_EXCHANGE_ACK);
		if (!ok) {
			warn("Not received RSRC_EXCHANGE_ACK!");
			return false;
		}

		info("model.materials = ", model.materials.size());
		for (const auto& mat : model.materials) {

			// Don't send the same material twice
			if (materialsSent.count(mat.name) != 0)
				continue;

			info("sending new material ", mat.name);

			ok = sendMaterial(clientSocket, mat);
			if (!ok) {
				err("Failed sending material");
				return false;
			}
			materialsSent.emplace(mat.name);

			ok = expectTCPMsg(clientSocket, packet.data(), 1, TcpMsgType::RSRC_EXCHANGE_ACK);
			if (!ok) {
				warn("Not received RSRC_EXCHANGE_ACK!");
				return false;
			}

			trySendTexture(mat.diffuseTex);
			trySendTexture(mat.specularTex, TextureFormat::GREY);
			trySendTexture(mat.normalTex);
		}
	}

	// Send lights
	for (const auto& light : server.resources.pointLights) {
		bool ok = sendPointLight(clientSocket, light);
		if (!ok) {
			err("Failed sending point light");
			return false;
		}
		ok = expectTCPMsg(clientSocket, packet.data(), 1, TcpMsgType::RSRC_EXCHANGE_ACK);
		if (!ok) {
			warn("Not received RSRC_EXCHANGE_ACK!");
			return false;
		}
	}

	info("Done sending data");

	return true;
}
