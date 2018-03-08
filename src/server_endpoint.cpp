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
#include "model.hpp"
#include "data.hpp"
#include "config.hpp"

/** Writes all possible vertices and indices, starting from `offset`, into `buffer` until it has room.
 *  Returns the number of elements that were not copied.
 */
template <long unsigned N>
static int writeAllPossible(std::array<uint8_t, N>& buffer,
		const std::vector<Vertex>& vertices, const std::vector<Index>& indices, int offset)
{
	unsigned bufferIdx = 0;

	for (unsigned i = offset; i < vertices.size(); ++i) {
		if (bufferIdx + sizeof(Vertex) >= buffer.size()) {
			// no more room in buffer
			return indices.size() + vertices.size() - i;
		}

		*(reinterpret_cast<Vertex*>(buffer.data() + bufferIdx)) = vertices[i];
		bufferIdx += sizeof(Vertex);
	}

	for (unsigned i = std::max(0ul, offset - vertices.size()); i < indices.size(); ++i) {
		if (bufferIdx + sizeof(Index) >= buffer.size()) {
			// no more room in buffer
			return indices.size() - i;
		}

		*(reinterpret_cast<Index*>(buffer.data() + bufferIdx)) = indices[i];
		bufferIdx += sizeof(Index);
	}

	return 0;
}

// TODO
const std::vector<Vertex> vertices = {
    {{0.0f, -0.5f, 0}, {1.0f, 0.0f, 0.0f}, {0, 1}},
    {{0.5f, 0.5f, 0}, {0.0f, 1.0f, 0.0f}, {1, 1}},
    {{-0.5f, 0.5f, 0}, {0.0f, 0.0f, 1.0f}, {0, 0}}
};
const std::vector<Index> indices = {
    0, 1, 2, 2, 3, 0
};
void ServerEndpoint::loopFunc() {

	//std::vector<Vertex> vertices;
	//std::vector<Index> indices;

	//loadModel("models/chalet.obj", vertices, indices);

	std::cerr << "Loaded " << vertices.size() << " vertices + " << indices.size() << " indices. "
		<< "Tot size = " << (vertices.size() * sizeof(Vertex) + indices.size() * sizeof(Index)) / 1024
		<< " KiB\n";

	using namespace std::chrono_literals;

	int64_t frameId = 0;
	int32_t packetId = 0;

	// Send datagrams
	while (!terminated) {

		// Start new frame
		// Send # vertices and indices to client, along with all the data that fit.
		FirstFrameData firstPacket;
		firstPacket.header.magic = cfg::PACKET_MAGIC;
		firstPacket.header.frameId = frameId;
		firstPacket.header.packetId = packetId;
		firstPacket.nVertices = vertices.size();
		firstPacket.nIndices = indices.size();
		int remaining = writeAllPossible(firstPacket.payload, vertices, indices, 0);

		for (int i = 0; i < 64; ++i)
			printf("%hhx ", firstPacket.payload[i]);
		std::cerr << "\nwriting packet " << frameId << ":" << packetId << "\n";
		if (write(socket, &firstPacket, sizeof(firstPacket)) < 0) {
			std::cerr << "could not write to remote: " << strerror(errno) << "\n";
		}

		while (remaining != 0) {
			// Send remaining data
			// Create new batch
			FrameData packet;
			packet.header.magic = cfg::PACKET_MAGIC;
			packet.header.frameId = frameId;
			packet.header.packetId = ++packetId;
			remaining = writeAllPossible(packet.payload, vertices, indices, remaining);
			std::cerr << "writing packet " << frameId << ":" << packetId << "\n";
			if (write(socket, &packet, sizeof(packet)) < 0) {
				std::cerr << "could not write to remote: " << strerror(errno) << "\n";
			}
		}

		/*
		*((uint32_t*) buffer) = MAGIC;
		*((uint64_t*)(buffer + 4)) = packetId;
		*((uint64_t*)(buffer + 12)) = uint64_t(vertices.size());
		*((uint64_t*)(buffer + 20)) = uint64_t(indices.size());
		std::cerr << "sending " << vertices.size() << " vertices and " << indices.size() << " indices\n";
		for (unsigned i = 0; i < vertices.size(); ++i) {
			*((Vertex*)(buffer + 28 + sizeof(Vertex)*i)) = vertices[i];
		}
		const auto indexOff = 28 + vertices.size() * sizeof(Vertex);
		for (unsigned i = 0; i < indices.size(); ++i) {
			*((uint32_t*)(buffer + indexOff + sizeof(Index)*i)) = indices[i];
		}

		const auto bytesSent = indexOff + sizeof(Index) * indices.size();
		std::cerr << "sent: " << bytesSent / 1024 << " KiB\n";

		if (write(socket, buffer, BUFSIZE) < 0) {
			std::cerr << "could not write to remote: " << strerror(errno) << "\n";
		}

		++packetId;*/

		++frameId;
		packetId = 0;

		std::this_thread::sleep_for(1.016s);
	}
}

