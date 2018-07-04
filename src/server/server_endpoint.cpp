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

// Delete ACKed messages from update queue
void deleteAckedUpdates(std::vector<uint32_t>& acks, std::vector<QueuedUpdate>& updates)
{
	auto p_last = updates.end() - 1;
	for (auto ack : acks) {
		for (auto p_read = updates.begin(); p_read != p_last + 1; ++p_read) {
			const auto& u = *p_read;
			bool found = false;
			switch (u.type) {
			case QueuedUpdate::Type::GEOM:
				found = u.data.geom.data.serialId == ack;
				break;
			default:
				err("Invalid update type ", int(u.type), " found in persistent updates!");
				throw;
			}
			if (!found)
				continue;

			// Erase this update
			*p_read = *p_last;
			--p_last;

			break;
		}
	}
	updates.erase(p_last + 1, updates.end());
	acks.clear();
}

void ServerActiveEndpoint::loopFunc()
{
	uint32_t packetGen = 0;

	std::array<uint8_t, cfg::PACKET_SIZE_BYTES> buffer = {};

	auto& updates = server.toClient.updates;

	// Send datagrams to the client
	while (!terminated) {

		std::unique_lock<std::mutex> ulk{ updates.mtx };
		if (updates.size() == 0) {
			// Wait for updates
			updates.cv.wait(ulk, [this, &updates = server.toClient.updates]() {
				return terminated || updates.size() > 0;
			});
		}

		auto offset = writeUdpHeader(buffer.data(), buffer.size(), packetGen);
		verbose("updates.size now = ", updates.size());

		// Send transitory updates
		auto w = updates.transitory.begin();
		for (auto it = w; it != updates.transitory.end(); ++it) {
			const auto& update = *it;
			const auto written = addUpdate(buffer.data(), buffer.size(), offset, update, server);

			if (written > 0) {
				// Packet was written into the buffer, erase it and go ahead
				offset += written;
			} else {
				// Not enough room: send the packet
				sendPacket(socket, buffer.data(), buffer.size());

				// Start with a new packet
				writeUdpHeader(buffer.data(), buffer.size(), packetGen);
				offset = sizeof(UdpHeader);

				// Don't erase this element yet: retry in next iteration
				if (it != w)
					*w = std::move(*it);
				++w;
			}
		}
		assert(ulk.owns_lock());
		updates.transitory.erase(w, updates.transitory.end());

		// Remove all persistent updates which were acked by the client
		{
			std::lock_guard<std::mutex> lock{ server.fromClient.acksReceivedMtx };
			deleteAckedUpdates(server.fromClient.acksReceived, updates.persistent);
		}

		if (updates.persistent.size() > 0)
			info("sending ", updates.persistent.size(), " persistent updates");

		// Send persistent updates
		for (auto it = updates.persistent.begin(); it != updates.persistent.end();) {
			const auto& update = *it;
			// GEOM updates are currently the only ACKed ones
			assert(update.type == QueuedUpdate::Type::GEOM);
			const auto written = addUpdate(buffer.data(), buffer.size(), offset, update, server);

			if (written > 0) {
				offset += written;
				++it;
			} else {
				// Not enough room: send the packet
				sendPacket(socket, buffer.data(), buffer.size());

				writeUdpHeader(buffer.data(), buffer.size(), packetGen);
				offset = sizeof(UdpHeader);
			}
		}

		if (offset > sizeof(UdpHeader)) {
			// Need to send the last packet
			sendPacket(socket, buffer.data(), buffer.size());
		}

		++packetGen;
	}
}

////////////////////////////////////////

void ServerPassiveEndpoint::loopFunc()
{
	// Receive client ACKs to (some of) our UDP messages

	while (!terminated) {
		std::array<uint8_t, cfg::PACKET_SIZE_BYTES> packetBuf = {};

		int bytesRead;
		if (!receivePacket(socket, packetBuf.data(), packetBuf.size(), &bytesRead))
			continue;

		if (bytesRead != sizeof(AckPacket)) {
			warn("Read bogus packet from client (",
				bytesRead,
				" bytes instead of expected ",
				sizeof(AckPacket),
				")");
			continue;
		}

		const auto packet = reinterpret_cast<const AckPacket*>(packetBuf.data());
		if (packet->msgType != UdpMsgType::ACK) {
			warn("Read bogus packet from client (type is ",
				packet->msgType,
				" instead of ",
				UdpMsgType::ACK,
				")");
			continue;
		}

		{
			std::lock_guard<std::mutex> lock{ server.fromClient.acksReceivedMtx };
			for (unsigned i = 0; i < packet->nAcks; ++i)
				server.fromClient.acksReceived.emplace_back(packet->acks[i]);
		}
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
		// Build the initial list of models to send to the client
		std::lock_guard<std::mutex> lock{ server.toClient.modelsToSendMtx };
		server.toClient.modelsToSend.reserve(server.resources.models.size());
		for (const auto& pair : server.resources.models)
			server.toClient.modelsToSend.emplace_back(pair.second);
	}

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
		server.passiveEP.startPassive(ip.c_str(), cfg::CLIENT_TO_SERVER_PORT, SOCK_DGRAM);
		server.passiveEP.runLoop();

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
	server.toClient.updates.cv.notify_all();
	info("Closing passiveEP");
	server.passiveEP.close();
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
