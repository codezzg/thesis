#include "client_endpoint.hpp"
#include <chrono>
#include <cstdlib>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <iostream>
#include <array>
#include <cstring>
#include <unistd.h>
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
	frameId = -1;
	uint64_t nVertices = 0;
	uint64_t nIndices = 0;
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
			nVertices = packet->header.nVertices;
			nIndices = packet->header.nIndices;
			*(reinterpret_cast<uint64_t*>(backBuffer)) = nVertices;
			*(reinterpret_cast<uint64_t*>(backBuffer) + 1) = nIndices;
		}

		uint8_t *payload = packet->payload.data();
		size_t payloadLen = packet->payload.size();

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
			//std::cerr << "received " << nBytesReceived << " / "
				//<< nVertices * sizeof(Vertex) + nIndices * sizeof(Index) << "\n";
			memcpy(buffer, backBuffer, BUFSIZE);
			bufferCopied = true;
		}
	}

	delete [] backBuffer;
	delete [] buffer;
}

const uint8_t* ClientPassiveEndpoint::peek() const {
	return bufferFilled && !terminated ? buffer : nullptr;
}


/////////////////////// Active EP
void ClientActiveEndpoint::loopFunc() {
	int64_t frameId = 0;
	uint64_t packetId = 0;

	using namespace std::literals::chrono_literals;

	auto delay = 0ms;

	while (!terminated) {
		const LimitFrameTime lft{ 33ms - delay };

		// Prepare data
		FrameData data;
		data.header.magic = cfg::PACKET_MAGIC;
		data.header.frameId = frameId;
		data.header.packetId = packetId;
		/* Payload:
		 * [0] CameraData (28 B)
		 */
		if (camera)
			serializeCamera(data.payload.data(), *camera);

		if (::write(socket, &data, sizeof(data)) < 0) {
			std::cerr << "could not write to remote: " << strerror(errno) << "\n";
		}

		++frameId;
		delay = lft.getFrameDelay();
	}
}
