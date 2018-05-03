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
#include "data.hpp"
#include "config.hpp"
#include "frame_utils.hpp"
#include "camera.hpp"
#include "serialization.hpp"
#include "server_appstage.hpp"

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

static constexpr std::size_t MEMSIZE = 1<<24;

static uint8_t* allocServerMemory() {
	return new uint8_t[MEMSIZE];
}

static void deallocServerMemory(uint8_t *mem) {
	delete [] mem;
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

	serverMemory = allocServerMemory();
	// This is used for storing the data to send, varying each frame.
	uint8_t *tmpMemory = serverMemory + MEMSIZE * 2 / 3;

	std::cerr << "cwd: " << xplatGetCwd() << std::endl;
	auto model = loadModel((xplatGetCwd() + "/models/mill.obj").c_str(), serverMemory);
	if (model.vertices == nullptr) {
		std::cerr << "Failed to load model.\n";
		return;
	}

	const auto vertices = reinterpret_cast<Vertex*>(serverMemory);
	const auto indices = reinterpret_cast<Index*>(serverMemory + sizeof(Vertex) * model.nVertices);
	std::cerr << "Loaded " << model.nVertices << " vertices + " << model.nIndices << " indices. "
		<< "Tot size = " << (model.nVertices * sizeof(Vertex) + model.nIndices * sizeof(Index)) / 1024
		<< " KiB\n";

	using namespace std::chrono_literals;

	std::mutex loopMtx;
	std::unique_lock<std::mutex> loopUlk{ loopMtx };

	auto delay = 0ms;

	// Send frame datagrams to the client
	while (!terminated) {
		const LimitFrameTime lft{ targetFrameTime - delay };

		// Wait for the new frame data from the client
		std::cerr << "Waiting for client data...\n";
		server.shared.loopCv.wait(loopUlk);
		//server.shared.loopCv.wait_for(loopMtx, 0.033s);

		std::cerr << "Received data from frame " << server.shared.clientFrame << "\n";

		int64_t frameId = -1;
		std::array<uint8_t, FrameData().payload.size()> clientData;
		{
			std::lock_guard<std::mutex> lock{ server.shared.clientDataMtx };
			frameId = server.shared.clientFrame;
			std::copy(server.shared.clientData.begin(),
					server.shared.clientData.end(), clientData.begin());
		}

		int nVertices = model.nVertices,
		    nIndices = model.nIndices;
		std::cerr << "v/i: " << nVertices << ", " << nIndices << " ---> ";
		transformVertices(model, clientData, tmpMemory, nVertices, nIndices);
		std::cerr << nVertices << ", " << nIndices << "\n";

		if (frameId >= 0)
			sendFrameData(frameId, tmpMemory, nVertices, nIndices);

		delay = lft.getFrameDelay();
	}

	deallocServerMemory(serverMemory);
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
		if (::send(socket, reinterpret_cast<const char*>(&packet), sizeof(packet), 0) < 0) {
			std::cerr << "could not write to remote: " << xplatGetErrorString() << "\n";
		}
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
			std::lock_guard<std::mutex> lock{ server.shared.clientDataMtx };
			memcpy(server.shared.clientData.data(), packet->payload.data(), packet->payload.size());
			server.shared.clientFrame = latestFrame;
		}
		server.shared.loopCv.notify_all();
	}
}

/////////////////////////////////////////

Server::Server() : activeEP(*this), passiveEP(*this) {}

void Server::run(const char *activeIp, int activePort, const char *passiveIp, int passivePort) {
	activeEP.startActive(activeIp, activePort);
	activeEP.runLoop();

	passiveEP.startPassive(passiveIp, passivePort);
	passiveEP.runLoop();
}

void Server::close() {
	activeEP.close();
	passiveEP.close();
}
