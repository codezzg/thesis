#include "server_endpoint.hpp"
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
#include <glm/gtx/string_cast.hpp>
#include <cstring>
#include "FPSCounter.hpp"
#include "model.hpp"
#include "tcp_messages.hpp"
#include "frame_data.hpp"
#include "config.hpp"
#include "frame_utils.hpp"
#include "camera.hpp"
#include "logging.hpp"
#include "serialization.hpp"
#include "clock.hpp"
#include "server_appstage.hpp"
#include "server.hpp"
#include "defer.hpp"

using namespace logging;
using namespace std::chrono_literals;
using shared::ResourceHeader;

/** Writes all possible vertices and indices, starting from `offset`-th byte,
 *  from `src` into `dst` until `dst` has room  or `src` is exhausted.
 *  @return the number of bytes that were copied so far, i.e. the next offset to use.
 *  NOTE: this operation may leave some unused trailing space in buffer if payload.size() is not
 *  a multiple of sizeof(Vertex) and sizeof(Index). The client, upon receiving
 *  the packet this buffer belongs to, should not just
 *  memcpy(dst, buffer, buffer.size()), but it must calculate the exact amount of bytes to pick from
 *  the buffer, or it will copy the unused garbage bytes too!
 */
template <std::size_t N>
static int writeAllPossible(std::array<uint8_t, N>& dst, const uint8_t *src,
		int nVertices, int nIndices, std::size_t offset)
{
	const auto srcSize = nVertices * sizeof(Vertex) + nIndices * sizeof(Index);
	auto srcIdx = offset;
	auto dstIdx = 0lu;
	while (srcIdx < srcSize && dstIdx < N) {
		const bool isVertex = srcIdx < static_cast<unsigned>(nVertices) * sizeof(Vertex);
		if (isVertex) {
			// Check for room
			if (dstIdx + sizeof(Vertex) > N) {
				info("[Warning] only filled ", dstIdx, "/", N, " dst bytes.");
				return srcIdx;
			}
			*(reinterpret_cast<Vertex*>(dst.data() + dstIdx)) =
					*(reinterpret_cast<const Vertex*>(src + srcIdx));
			dstIdx += sizeof(Vertex);
			srcIdx += sizeof(Vertex);
		} else {
			// Check for room
			if (dstIdx + sizeof(Index) > N) {
				info("[Warning] only filled ", dstIdx, "/", N, " dst bytes.");
				return srcIdx;
			}
			*(reinterpret_cast<Index*>(dst.data() + dstIdx)) =
					*(reinterpret_cast<const Index*>(src + srcIdx));
			dstIdx += sizeof(Index);
			srcIdx += sizeof(Index);
		}
	}

	// If we arrived here, we filled every last byte of the payload with no waste.
	return srcIdx;
}

// TODO
//const std::vector<Vertex> vertices = {
	////{ {0, 1, 2}, {3, 4, 5}, {6, 7} },
	//{{0.0f, -0.5f, 0}, {1.0f, 0.0f, 0.0f}, {0, 1}},
	//{{0.5f, 0.5f, 0}, {0.0f, 1.0f, 0.0f}, {1, 1}},
	//{{-0.5f, 0.5f, 0}, {0.0f, 0.0f, 1.0f}, {0, 0}}
//};
//const std::vector<Index> indices = {
	////8
	//0, 1, 2, 2, 3, 0
//};
void ServerActiveEndpoint::loopFunc() {

	auto delay = 0ms;
	auto& shared = server.sharedData;

	std::mutex loopMtx;

	FPSCounter fps;
	fps.start();

	auto& clock = Clock::instance();

	info("Active Endpoint targetFrameTime = ", targetFrameTime.count(), " ms");

	uint64_t frameId = 0;

	// Send frame datagrams to the client
	while (!terminated) {
		const LimitFrameTime lft{ targetFrameTime - delay };
		verbose("Frametime = ", asSeconds(targetFrameTime - delay) * 1000, " ms");

		++frameId;

		// Wait for the new frame data from the client
		debug("Waiting for client data...");
		/*std::unique_lock<std::mutex> loopUlk{ loopMtx };
		shared.loopCv.wait(loopUlk);*/
		//shared.loopCv.wait_for(loopMtx, 0.033s);

		debug("Received data from frame ", shared.clientFrame);

		//int64_t frameId = -1;
		std::array<uint8_t, FrameData().payload.size()> clientData;
		{
			std::lock_guard<std::mutex> lock{ shared.clientDataMtx };
			debug("Server frame = ", frameId, ", client frame = ", shared.clientFrame);
			//frameId = shared.clientFrame;
			std::copy(shared.clientData.begin(), shared.clientData.end(), clientData.begin());
		}

		// TODO: multiple models
		auto& model = server.resources.models.begin()->second;
		int nVertices = model.nVertices,
		    nIndices = model.nIndices;
		log(LOGLV_DEBUG, false, "v/i: ", nVertices, ", ", nIndices, " ---> ");
		transformVertices(model, clientData, memory, memsize, nVertices, nIndices);
		debug(nVertices, ", ", nIndices);

		if (frameId >= 0)
			sendFrameData(frameId, memory, nVertices, nIndices);

		fps.addFrame();
		fps.report();
		clock.update(asSeconds(lft.getFrameDuration()));
		delay = lft.getFrameDelay();
	}
}

void ServerActiveEndpoint::sendFrameData(int64_t frameId, uint8_t *buffer, int nVertices, int nIndices) {
	// Start new frame
	size_t totSent = 0;
	int nPacketsSent = 0;
	size_t offset = 0;
	int32_t packetId = 0;

	const std::size_t totBytes = nVertices * sizeof(Vertex) + nIndices * sizeof(Index);
	while (!terminated && offset < totBytes) {
		// Create new packet
		FrameData packet;
		packet.header.magic = cfg::PACKET_MAGIC;
		packet.header.frameId = frameId;
		packet.header.packetId = packetId;
		packet.header.phead.nVertices = nVertices;
		packet.header.phead.nIndices = nIndices;
		//const auto preOff = offset;
		offset = writeAllPossible(packet.payload, buffer, nVertices, nIndices, offset);
		//std::cerr << "offset: " << offset << " (copied " << offset - preOff << " bytes)\n";
		//std::cerr << "writing packet " << frameId << ":" << packetId << "\n";
		//dumpPacket("server.dump", packet);

		sendPacket(socket, reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));

		totSent += packet.payload.size();
		++nPacketsSent;
		++packetId;
	}
	//std::cerr << "Sent total " << totSent << " bytes (" << nPacketsSent << " packets).\n";
}

////////////////////////////////////////


// Receives client parameters wherewith the server shall calculate the primitives to send during next frame
void ServerPassiveEndpoint::loopFunc() {
	// Track the latest frame we received
	int64_t latestFrame = -1;
	auto& shared = server.sharedData;
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

		// Update shared data
		{
			std::lock_guard<std::mutex> lock{ shared.clientDataMtx };
			memcpy(shared.clientData.data(), packet->payload.data(), packet->payload.size());
			shared.clientFrame = latestFrame;
		}
		shared.loopCv.notify_all();
	}
}

/////////////////////////////////////////

void ServerReliableEndpoint::loopFunc() {
	// FIXME?
	constexpr auto MAX_CLIENTS = 1;

	info("Listening...");
	::listen(socket, MAX_CLIENTS);

	while (!terminated) {

		sockaddr_in clientAddr;
		socklen_t clientLen = sizeof(clientAddr);

		auto clientSocket = ::accept(socket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
		if (clientSocket == -1) {
			err("Error: couldn't accept connection.");
			continue;
		}

		info("Accepted connection from ", inet_ntoa(clientAddr.sin_addr));
		//std::thread listener(&ServerReliableEndpoint::listenTo, this, clientSocket, clientAddr);
		//listener.detach();

		// Single client
		listenTo(clientSocket, clientAddr);
	}
}

/** This task listens for keepalives and updates `latestPing` with the current time every time it receives one. */
static void keepaliveTask(socket_t clientSocket, std::condition_variable& cv,
		std::chrono::time_point<std::chrono::system_clock>& latestPing)
{
	std::array<uint8_t, 1> buffer = {};

	while (true) {
		MsgType type;
		if (!receiveTCPMsg(clientSocket, buffer.data(), buffer.size(), type)) {
			cv.notify_one();
			break;
		}

		if (type == MsgType::KEEPALIVE)
			latestPing = std::chrono::system_clock::now();
	}
}

void ServerReliableEndpoint::listenTo(socket_t clientSocket, sockaddr_in clientAddr) {

	const auto readableAddr = inet_ntoa(clientAddr.sin_addr);

	{
		// Connection prelude (one-time stuff)

		std::array<uint8_t, 1> buffer = {};

		// Perform handshake
		if (!expectTCPMsg(clientSocket, buffer.data(), buffer.size(), MsgType::HELO))
			goto dropclient;

		if (!sendTCPMsg(clientSocket, MsgType::HELO_ACK))
			goto dropclient;

		// Send one-time data
		info("Sending one time data...");
		if (!sendTCPMsg(clientSocket, MsgType::START_RSRC_EXCHANGE))
			goto dropclient;

		if (!expectTCPMsg(clientSocket, buffer.data(), buffer.size(), MsgType::RSRC_EXCHANGE_ACK))
			goto dropclient;

		if (!sendOneTimeData(clientSocket))
			goto dropclient;

		//std::exit(0);

		if (!sendTCPMsg(clientSocket, MsgType::END_RSRC_EXCHANGE))
			goto dropclient;

		// Wait for ready signal from client
		if (!expectTCPMsg(clientSocket, buffer.data(), buffer.size(), MsgType::READY))
			goto dropclient;

		// Starts UDP loops and send ready to client
		server.passiveEP.startPassive(ip.c_str(), cfg::CLIENT_TO_SERVER_PORT, SOCK_DGRAM);
		server.activeEP.startActive(readableAddr, cfg::SERVER_TO_CLIENT_PORT, SOCK_DGRAM);
		server.passiveEP.runLoop();
		server.activeEP.runLoop();

		if (!sendTCPMsg(clientSocket, MsgType::READY))
			goto dropclient;
	}

	{
		// Periodically check keepalive, or drop the client
		std::chrono::time_point<std::chrono::system_clock> latestPing;
		std::thread keepaliveThread{ keepaliveTask, clientSocket, std::ref(loopCv), std::ref(latestPing) };

		const auto& roLatestPing = latestPing;
		std::unique_lock<std::mutex> loopUlk{ loopMtx };
		const auto interval = std::chrono::seconds{ cfg::SERVER_KEEPALIVE_INTERVAL_SECONDS };

		while (true) {
			if (loopCv.wait_for(loopUlk, interval) == std::cv_status::no_timeout)
				break;

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
		sendTCPMsg(clientSocket, MsgType::DISCONNECT);
	}
	server.sharedData.loopCv.notify_all();
	server.passiveEP.close();
	server.activeEP.close();
	xplatSockClose(clientSocket);
}

void ServerReliableEndpoint::onClose() {
	loopCv.notify_all();
}

static bool sendMaterial(socket_t clientSocket, const Material& material) {

	ResourceHeader<shared::Material> packet;
	packet.type = MsgType::RSRC_TYPE_MATERIAL;
	packet.size = sizeof(packet); // we have no additional payload
	packet.head.name = material.name;
	packet.head.diffuseTex = material.diffuseTex.length() > 0 ? sid(material.diffuseTex) : SID_NONE;
	packet.head.specularTex = material.specularTex.length() > 0 ? sid(material.specularTex) : SID_NONE;

	info("packet: { type = ", packet.type, ", size = ", packet.size, ", name = ",
		packet.head.name, " (", sidToString(packet.head.name), "), diffuse = ",
		packet.head.diffuseTex, ", specular = ", packet.head.specularTex, " }");

	return sendPacket(clientSocket, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));
}

bool ServerReliableEndpoint::sendOneTimeData(socket_t clientSocket) {

	/* Send all materials (which are basically maps (mat id) => { (diffuse): tex id, (specular): tex id, ... })
	 * and all textures data.
	 */

	std::array<uint8_t, 1> packet = {};
	std::unordered_set<std::string> texturesSent;

	const auto shouldSendTexture = [&texturesSent] (const std::string& texName) {
		return texName.length() > 0 && texturesSent.count(texName) == 0;
	};

	info("# models loaded = ", server.resources.models.size());
	for (const auto& modpair : server.resources.models) {

		const auto& model = modpair.second;

		info("model.materials = ", model.materials.size());
		for (const auto& mat : model.materials) {

			info("sending new material ", mat.name);

			bool ok = sendMaterial(clientSocket, mat);
			if (!ok) {
				err("Failed sending material");
				return false;
			}

			ok = expectTCPMsg(clientSocket, packet.data(), 1, MsgType::RSRC_EXCHANGE_ACK);
			if (!ok) {
				warn("Not received RSRC_EXCHANGE_ACK!");
				return false;
			}

			if (shouldSendTexture(mat.diffuseTex)) {
				info("* sending diffuse texture");

				ok = sendTexture(clientSocket, mat.diffuseTex, shared::TextureFormat::RGBA);
				if (!ok) {
					err("sendOneTimeData: failed");
					return false;
				}

				ok = expectTCPMsg(clientSocket, packet.data(), 1, MsgType::RSRC_EXCHANGE_ACK);
				if (!ok) {
					warn("Not received RSRC_EXCHANGE_ACK!");
					return false;
				}

				texturesSent.emplace(mat.diffuseTex);
			}

			if (shouldSendTexture(mat.specularTex)) {
				info("* sending specular texture");

				ok = sendTexture(clientSocket, mat.specularTex, shared::TextureFormat::GREY);
				if (!ok) {
					err("sendOneTimeData: failed");
					return false;
				}

				ok = expectTCPMsg(clientSocket, packet.data(), 1, MsgType::RSRC_EXCHANGE_ACK);
				if (!ok) {
					warn("Not received RSRC_EXCHANGE_ACK!");
					return false;
				}

				texturesSent.emplace(mat.specularTex);
			}
		}
	}

	info("Done sending data");

	return true;
}

bool ServerReliableEndpoint::sendTexture(socket_t clientSocket, const std::string& texName,
		shared::TextureFormat format)
{
	using shared::TextureHeader;

	std::array<uint8_t, cfg::PACKET_SIZE_BYTES> packet;

	// Load the texture, and unload it as we finished using it
	server.resources.loadTexture(texName.c_str());
	DEFER([this] () {
		server.resources.textures.clear();
		server.resources.allocator.deallocLatest();
	});

	// Prepare header
	const auto texNameSid = sid(texName);
	const auto& texture = server.resources.textures[texNameSid];
	ResourceHeader<TextureHeader> header;
	header.type = MsgType::RSRC_TYPE_TEXTURE;
	header.size = texture.size;
	header.head.name = texNameSid;
	header.head.format = format;

	info("Sending texture ", texName, " (", texNameSid, ")");

	// Put header into packet
	info("header: { type = ", header.type, ", size = ", header.size, ", name = ",
			header.head.name, ", format = ", int(header.head.format), " }");
	memcpy(packet.data(), reinterpret_cast<uint8_t*>(&header), sizeof(header));

	// Fill remaining space with payload
	auto len = std::min(texture.size, packet.size() - sizeof(header));
	memcpy(packet.data() + sizeof(header), texture.data, len);

	if (!sendPacket(clientSocket, packet.data(), len + sizeof(header)))
		return false;

	std::size_t bytesSent = len;
	// Send more packets with remaining payload if needed
	while (bytesSent < texture.size) {
		auto len = std::min(texture.size - bytesSent, cfg::PACKET_SIZE_BYTES);
		if (!sendPacket(clientSocket, reinterpret_cast<uint8_t*>(texture.data) + bytesSent, len))
			return false;
		bytesSent += len;
	}

	return true;
}
