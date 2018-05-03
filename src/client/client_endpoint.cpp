#include "client_endpoint.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <array>
#include <cstring>
#include "data.hpp"
#include "config.hpp"
#include "vertex.hpp"
#include "camera.hpp"
#include "serialization.hpp"
#include "frame_utils.hpp"

static constexpr auto BUFSIZE = 1<<24;

void ClientPassiveEndpoint::loopFunc() {

	// This will be filled like this:
	// [(64b)nVertices|(64b)nIndices|vertices|indices]
	buffer = new uint8_t[BUFSIZE];
	auto backBuffer = new uint8_t[BUFSIZE];

	uint64_t nBytesReceived = 0;
	bool bufferCopied = false;

	// Receive datagrams
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
			// Clear current buffer, start receiving new data
			memset(backBuffer, -1, BUFSIZE);
			bufferFilled = false;
			bufferCopied = false;
			nBytesReceived = 0;

			// Update n vertices and indices
			nVertices = packet->header.phead.nVertices;
			nIndices = packet->header.phead.nIndices;
			*(reinterpret_cast<uint64_t*>(backBuffer)) = nVertices;
			*(reinterpret_cast<uint64_t*>(backBuffer) + 1) = nIndices;
		}

		uint8_t *payload = packet->payload.data();
		auto payloadLen = packet->payload.size();

		//dumpPacket("client.dump", *packet);

		// Compute the offset to insert data at
		const size_t offset = 2 * sizeof(uint64_t) + packet->header.packetId * packet->payload.size();
		//std::cerr << "received packet " << frameId << ":" << packet->header.packetId << "; offset = " << offset << "\n";

		// Insert data into the buffer
		memcpy(backBuffer + offset, payload, payloadLen);

		nBytesReceived += payloadLen;
		//std::cerr << "payload len = " << payloadLen << "\n";
		bufferFilled = nBytesReceived >= nVertices * sizeof(Vertex) + nIndices * sizeof(Index);

		if (bufferFilled && !bufferCopied) {
			// May need to wait to finish retreiving the previously acquired buffer.
			std::lock_guard<std::mutex> lock{ bufMtx };

			memcpy(buffer, backBuffer, BUFSIZE);
			bufferCopied = true;
		}
	}

	delete [] backBuffer;
	delete [] buffer;
}

void ClientPassiveEndpoint::retreive(PayloadHeader& phead, Vertex *outVBuf, Index *outIBuf) {
	std::lock_guard<std::mutex> lock{ bufMtx };
	memcpy(reinterpret_cast<void*>(&phead), buffer, sizeof(PayloadHeader));
	memcpy(outVBuf, buffer + sizeof(PayloadHeader), nVertices * sizeof(Vertex));
	memcpy(outIBuf, buffer + sizeof(PayloadHeader) + nVertices * sizeof(Vertex), nIndices * sizeof(Index));
}


/////////////////////// Active EP
void ClientActiveEndpoint::loopFunc() {
	int64_t frameId = 0;
	uint32_t packetId = 0;

	using namespace std::literals::chrono_literals;

	auto delay = 0ms;

	while (!terminated) {
		const LimitFrameTime lft{ targetFrameTime - delay };

		// Prepare data
		FrameData data;
		data.header.magic = cfg::PACKET_MAGIC;
		data.header.frameId = frameId;
		data.header.packetId = packetId;
		/* Payload:
		 * [0] CameraData (28 B)
		 */
		if (camera)
			serializeCamera(data.payload, *camera);

		if (::send(socket, reinterpret_cast<const char*>(&data), sizeof(data), 0) < 0) {
			std::cerr << "could not write to remote: " << strerror(errno) << "\n";
		}

		++frameId;
		delay = lft.getFrameDelay();
	}
}
