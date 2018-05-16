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
#include "data.hpp"
#include "config.hpp"
#include "frame_utils.hpp"
#include "camera.hpp"
#include "logging.hpp"
#include "serialization.hpp"
#include "clock.hpp"
#include "server_appstage.hpp"
#include "server.hpp"

using namespace logging;
using namespace std::chrono_literals;

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

	// Send frame datagrams to the client
	while (!terminated) {
		const LimitFrameTime lft{ targetFrameTime - delay };

		// Wait for the new frame data from the client
		debug("Waiting for client data...");
		std::unique_lock<std::mutex> loopUlk{ loopMtx };
		shared.loopCv.wait(loopUlk);
		//shared.loopCv.wait_for(loopMtx, 0.033s);

		debug("Received data from frame ", shared.clientFrame);

		int64_t frameId = -1;
		std::array<uint8_t, FrameData().payload.size()> clientData;
		{
			std::lock_guard<std::mutex> lock{ shared.clientDataMtx };
			frameId = shared.clientFrame;
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

	while (!terminated) {
		std::array<uint8_t, sizeof(FrameData)> packetBuf = {};
		if (!receivePacket(socket, packetBuf.data(), packetBuf.size()))
			continue;

		if (!validateUDPPacket(packetBuf.data(), latestFrame))
			continue;

		const auto packet = reinterpret_cast<FrameData*>(packetBuf.data());
		debug("Received packet ", packet->header.frameId);
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
	// FIXME
	constexpr auto MAX_CLIENTS = 1;

	while (!terminated) {
		info("Listening...");
		::listen(socket, MAX_CLIENTS);

		sockaddr_in clientAddr;
		socklen_t clientLen = sizeof(clientAddr);

		auto clientSock = ::accept(socket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
		if (clientSock == -1) {
			err("Error: couldn't accept connection.");
			continue;
		}

		info("Accepted connection from ", inet_ntoa(clientAddr.sin_addr));
		//std::thread listener(&ServerReliableEndpoint::listenTo, this, clientSock, clientAddr);
		//listener.detach();

		// Single client
		listenTo(clientSock, clientAddr);
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

		buffer[0] = msg2byte(MsgType::HELO_ACK);
		if (!sendPacket(clientSocket, buffer.data(), buffer.size()))
			goto dropclient;

		// Send one-time data
		// TODO

		// Wait for ready signal from client
		if (!expectTCPMsg(clientSocket, buffer.data(), buffer.size(), MsgType::READY))
			goto dropclient;

		// Starts UDP loops and send ready to client
		server.passiveEP.startPassive(cfg::SERVER_PASSIVE_IP, cfg::SERVER_PASSIVE_PORT, SOCK_DGRAM);
		server.activeEP.startActive(cfg::SERVER_ACTIVE_IP, cfg::SERVER_ACTIVE_PORT, SOCK_DGRAM);
		server.passiveEP.runLoop();
		server.activeEP.runLoop();

		buffer[0] = msg2byte(MsgType::READY);
		if (!sendPacket(clientSocket, buffer.data(), buffer.size()))
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
		std::array<uint8_t, 1> buffer = { msg2byte(MsgType::DISCONNECT) };
		sendPacket(clientSocket, buffer.data(), buffer.size());
	}
	server.sharedData.loopCv.notify_all();
	server.passiveEP.close();
	server.activeEP.close();
	xplatSockClose(clientSocket);
}

void ServerReliableEndpoint::onClose() {
	loopCv.notify_all();
}
