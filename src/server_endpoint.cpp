#include "server_endpoint.hpp"
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <iostream>
#include <thread>
#include <vector>
#include "Vertex.hpp"
// TODO cross-platform
#include <unistd.h>
#include <cstring>
#include "model.hpp"
#include "data.hpp"
#include "config.hpp"

/** Writes all possible vertices and indices, starting from `offset`-th byte,
 *  from `src` into `dst` until `dst` has room  or `src` is exhausted.
 *  @return the number of bytes that were copied so far, i.e. the next offset to use.
 *  NOTE: this operation may leave some unused trailing space in buffer if payload.size() is not
 *  a multiple of sizeof(Vertex) and sizeof(Index). The client, upon receiving
 *  the packet this buffer belongs to, should not just
 *  memcpy(dst, buffer, buffer.size()), but it must calculate the exact amount of bytes to pick from
 *  the buffer, or it will copy the unused garbage bytes too!
 */
template <size_t N>
static int writeAllPossible(std::array<uint8_t, N>& dst, const uint8_t *src,
		int nVertices, int nIndices, size_t offset)
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

constexpr size_t MEMSIZE = 1<<24;

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

	uint8_t *serverMemory = allocServerMemory();

	int nVertices = 0,
	    nIndices = 0;
	if (!loadModel("models/mill.obj", serverMemory, nVertices, nIndices))
		return;

	const auto vertices = reinterpret_cast<Vertex*>(serverMemory);
	const auto indices = reinterpret_cast<Index*>(serverMemory + sizeof(Vertex) * nVertices);
	std::cerr << "Loaded " << nVertices << " vertices + " << nIndices << " indices. "
		<< "Tot size = " << (nVertices * sizeof(Vertex) + nIndices * sizeof(Index)) / 1024
		<< " KiB\n";

	//vertices.resize(30);
	//indices.resize(70);

	using namespace std::chrono_literals;

	int64_t frameId = 0;

	// Send datagrams
	while (!terminated) {

		// Start new frame
		int totSent = 0;
		int nPacketsSent = 0;
		auto offset = 0lu;
		int32_t packetId = 0;

		const size_t totBytes = nVertices * sizeof(Vertex) + nIndices * sizeof(Index);
		while (offset < totBytes) {
			// Create new packet
			FrameData packet;
			packet.header.magic = cfg::PACKET_MAGIC;
			packet.header.frameId = frameId;
			packet.header.packetId = packetId;
			packet.header.nVertices = nVertices;
			packet.header.nIndices = nIndices;
			const auto preOff = offset;
			offset = writeAllPossible(packet.payload, serverMemory, nVertices, nIndices, offset);
			//std::cerr << "offset: " << offset << " (copied " << offset - preOff << " bytes)\n";
			//std::cerr << "writing packet " << frameId << ":" << packetId << "\n";
			//dumpPacket("server.dump", packet);
			if (write(socket, &packet, sizeof(packet)) < 0) {
				std::cerr << "could not write to remote: " << strerror(errno) << "\n";
			}
			totSent += packet.payload.size();
			++nPacketsSent;
			++packetId;
		}

		//std::cerr << "Sent total " << totSent << " bytes (" << nPacketsSent << " packets).\n";

		++frameId;

		std::this_thread::sleep_for(0.033s);
	}

	deallocServerMemory(serverMemory);
}

void ServerPassiveEndpoint::loopFunc() {
	// This buffer contains a single packet
	buffer = new uint8_t[sizeof(FrameData)];
	int64_t frameId = -1;

	while (!terminated) {
		std::array<uint8_t, sizeof(FrameData)> packetBuf = {};
		if (!receivePacket(socket, packetBuf.data(), packetBuf.size()))
			continue;

		if (!validatePacket(packetBuf.data(), frameId))
			continue;

		const auto packet = reinterpret_cast<FrameData*>(packetBuf.data());
		// Update frame if necessary
		if (packet->header.frameId > frameId) {
			frameId = packet->header.frameId;
		}

		memcpy(buffer, packet->payload.data(), packet->payload.size());
	}

	delete [] buffer;
}


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
