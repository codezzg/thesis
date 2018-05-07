#include "server_endpoint.hpp"
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
#include <glm/gtx/string_cast.hpp>
#include <cstring>
#include "model.hpp"
#include "tcp_messages.hpp"
#include "data.hpp"
#include "config.hpp"
#include "frame_utils.hpp"
#include "camera.hpp"
#include "serialization.hpp"
#include "server_appstage.hpp"
#include "server.hpp"

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
				std::cerr << "[Warning] only filled " << dstIdx << "/" << N << " dst bytes.\n";
				return srcIdx;
			}
			*(reinterpret_cast<Vertex*>(dst.data() + dstIdx)) =
					*(reinterpret_cast<const Vertex*>(src + srcIdx));
			dstIdx += sizeof(Vertex);
			srcIdx += sizeof(Vertex);
		} else {
			// Check for room
			if (dstIdx + sizeof(Index) > N) {
				std::cerr << "[Warning] only filled " << dstIdx << "/" << N << " dst bytes.\n";
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

	std::mutex loopMtx;
	std::unique_lock<std::mutex> loopUlk{ loopMtx };

	auto delay = 0ms;
	auto& shared = server.sharedData;

	// Send frame datagrams to the client
	while (!terminated) {
		const LimitFrameTime lft{ targetFrameTime - delay };

		// Wait for the new frame data from the client
		std::cerr << "Waiting for client data...\n";
		shared.loopCv.wait(loopUlk);
		//shared.loopCv.wait_for(loopMtx, 0.033s);

		std::cerr << "Received data from frame " << shared.clientFrame << "\n";

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
		std::cerr << "v/i: " << nVertices << ", " << nIndices << " ---> ";
		transformVertices(model, clientData, memory, memsize, nVertices, nIndices);
		std::cerr << nVertices << ", " << nIndices << "\n";

		if (frameId >= 0)
			sendFrameData(frameId, memory, nVertices, nIndices);

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
	while (offset < totBytes) {
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

		sendPacket(socket, reinterpret_cast<const char*>(&packet), sizeof(packet));

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

		if (!validatePacket(packetBuf.data(), latestFrame))
			continue;

		const auto packet = reinterpret_cast<FrameData*>(packetBuf.data());
		std::cerr << "Received packet " << packet->header.frameId << "\n";
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
		::listen(socket, MAX_CLIENTS);

		sockaddr_in clientAddr;
		socklen_t clientLen = sizeof(clientAddr);

		auto clientSock = ::accept(socket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
		if (clientSock == -1) {
			std::cerr << "[ ERROR ] couldn't accept connection.\n";
			continue;
		}

		std::cout << "Accepted connection from " << inet_ntoa(clientAddr.sin_addr) << "\n";
		//std::thread listener(&ServerReliableEndpoint::listenTo, this, clientSock, clientAddr);
		//listener.detach();

		// Single client
		listenTo(clientSock, clientAddr);
	}
}


/** Receives a message from `socket` into `buffer` and fills the `msgType` variable according to the
 *  type of message received (i.e. the message header)
 */
static bool receiveClientMsg(socket_t socket, uint8_t *buffer, std::size_t bufsize, MsgType& msgType) {

	msgType = MsgType::UNKNOWN;

	const auto count = recv(socket, buffer, bufsize, 0);
	if (count < 0) {
		std::cerr << "Error receiving message: [" << count << "] " << xplatGetErrorString() << "\n";
		return false;
	} else if (count == sizeof(buffer)) {
		std::cerr << "Warning: datagram was truncated as it's too large.\n";
		return false;
	} else if (count == 0) {
		std::cerr << "Received EOF.\n";
		return false;
	}

	// TODO: validate message header

	// Check type of message (TODO) -- currently the message type is determined by its first byte.
	msgType = byte2msg(buffer[0]);

	return true;
}

static bool expectClientMsg(socket_t socket, uint8_t *buffer, std::size_t bufsize, MsgType expectedType) {
	MsgType type;
	return receiveClientMsg(socket, buffer, bufsize, type) && type == expectedType;
}

/** This task listens for keepalives and updates `latestPing` with the current time every time it receives one. */
static void keepaliveTask(socket_t clientSocket, std::chrono::time_point<std::chrono::system_clock>& latestPing) {
	std::array<uint8_t, 1> buffer = {};

	while (true) {
		if (!expectClientMsg(clientSocket, buffer.data(), buffer.size(), MsgType::KEEPALIVE))
			continue;

		latestPing = std::chrono::system_clock::now();
	}
}

void ServerReliableEndpoint::listenTo(socket_t clientSocket, sockaddr_in clientAddr) {

	const auto readableAddr = inet_ntoa(clientAddr.sin_addr);

	{
		// Connection prelude (one-time stuff)

		std::array<uint8_t, 256> buffer = {};

		// Perform handshake
		if (!expectClientMsg(clientSocket, buffer.data(), buffer.size(), MsgType::HELO))
			goto dropclient;

		// Send one-time data
		// TODO

		// Wait for ready signal from client
		if (!expectClientMsg(clientSocket, buffer.data(), buffer.size(), MsgType::READY))
			goto dropclient;
	}


	{
		// Periodically check keepalive, or drop the client
		std::chrono::time_point<std::chrono::system_clock> latestPing;
		std::thread keepaliveThread{ keepaliveTask, clientSocket, std::ref(latestPing) };
		const auto& roLatestPing = latestPing;

		std::mutex loopMtx;
		std::unique_lock<std::mutex> loopUlk{ loopMtx };
		const auto interval = std::chrono::seconds{ cfg::SERVER_KEEPALIVE_INTERVAL_SECONDS };
		while (true) {
			loopCv.wait_for(loopUlk, interval);

			// Verify the client has pinged us more recently than SERVER_KEEPALIVE_INTERVAL_SECONDS
			const auto now = std::chrono::system_clock::now();
			if (std::chrono::duration_cast<std::chrono::seconds>(now - roLatestPing) > interval) {
				// drop the client
				std::cerr << "Keepalive timeout.\n";
				break;
			}
		}
		if (keepaliveThread.joinable())
			keepaliveThread.join();
	}

dropclient:
	std::cerr << "TCP: TCP: Dropping client " << readableAddr << "\n";
	xplatSockClose(clientSocket);
}

void ServerReliableEndpoint::onClose() {
	loopCv.notify_all();
}
