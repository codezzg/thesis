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
#include "Vertex.hpp"
#include "camera.hpp"

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

	// Receive datagrams
	while (!terminated) {
		static_assert(sizeof(FirstFrameData) >= sizeof(FrameData),
				"size of FrameData is larger than FirstFrameData!");
		std::array<uint8_t, sizeof(FirstFrameData) + 1> packetBuf = {};
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
			nBytesReceived = 0;
		}

		uint8_t *payload = nullptr;
		size_t payloadLen = 0;
		// Distinguish first packet from others
		if (packet->header.packetId == 0) {
			const auto data = reinterpret_cast<FirstFrameData*>(packetBuf.data());
			nVertices = data->nVertices;
			nIndices = data->nIndices;
			std::cerr << "[" << frameId << "] received nvertices = " << nVertices << ", nindices = " << nIndices << "\n";
			*(reinterpret_cast<uint64_t*>(backBuffer)) = nVertices;
			*(reinterpret_cast<uint64_t*>(backBuffer) + 1) = nIndices;
			payload = data->payload.data();
			payloadLen = data->payload.size();
		} else {
			payload = packet->payload.data();
			payloadLen = packet->payload.size();
		}

		std::cerr << "received packet " << frameId << ":" << packet->header.packetId << "\n";

		// Compute the offset to insert data at
		const size_t offset = 2 * sizeof(uint64_t) + (packet->header.packetId == 0
			? 0
			: (packet->header.packetId * sizeof(packet->payload)
				+ sizeof(FirstFrameData::payload))); // payload of first packet is different

		// Insert data into the buffer
		memcpy(backBuffer + offset, payload, payloadLen);

		nBytesReceived += payloadLen;
		bufferFilled = nBytesReceived >= nVertices * sizeof(Vertex) + nIndices * sizeof(Index);
		//std::cerr << "received " << nBytesReceived << " / " << nVertices * sizeof(Vertex) + nIndices * sizeof(Index) << "\n";
		if (bufferFilled)
			memcpy(buffer, backBuffer, BUFSIZE);
	}

	delete [] backBuffer;
	delete [] buffer;
}

const uint8_t* ClientPassiveEndpoint::peek() const {
	return bufferFilled && !terminated ? buffer : nullptr;
}

static void insertCameraData(uint8_t *buffer, const Camera& camera) {
	/*
	 * CameraData:
	 * [0] position.x
	 * [1] position.y
	 * [2] position.z
	 * [3] rotation.w
	 * [4] rotation.x
	 * [5] rotation.y
	 * [6] rotation.z
	 */
	buffer[0] = camera.position.x;
	buffer[1] = camera.position.y;
	buffer[2] = camera.position.z;
	buffer[3] = camera.rotation.w;
	buffer[4] = camera.rotation.x;
	buffer[5] = camera.rotation.y;
	buffer[6] = camera.rotation.z;
}

void ClientActiveEndpoint::loopFunc() {
	int64_t frameId = -1;
	uint64_t packetId = 0;

	using namespace std::literals::chrono_literals;

	while (!terminated) {
		// Prepare data
		FrameData data;
		data.header.magic = cfg::PACKET_MAGIC;
		data.header.frameId = frameId;
		data.header.packetId = packetId;
		/* Payload:
		 * [0] CameraData (28 B)
		 */
		if (camera)
			insertCameraData(data.payload.data(), *camera);

		if (::write(socket, &data, sizeof(data)) < 0) {
			std::cerr << "could not write to remote: " << strerror(errno) << "\n";
		}

		std::this_thread::sleep_for(0.033s);
	}
}
