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
#include "tcp_messages.hpp"
#include "logging.hpp"

using namespace logging;
using namespace std::literals::chrono_literals;

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

		if (!validateUDPPacket(packetBuf.data(), frameId))
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

		sendPacket(socket, reinterpret_cast<const uint8_t*>(&data), sizeof(data));

		++frameId;
		delay = lft.getFrameDelay();
	}
}


/////////////////////// ReliableEP

bool ClientReliableEndpoint::await(std::chrono::seconds timeout) {
	std::mutex mtx;
	std::unique_lock<std::mutex> ulk{ mtx };
	return cv.wait_for(ulk, timeout) == std::cv_status::no_timeout;
}


static bool performHandshake(socket_t socket) {

	std::array<uint8_t, 1> buf = {};

	// send HELO message
	buf[0] = msg2byte(MsgType::HELO);
	if (!sendPacket(socket, buf.data(), buf.size()))
		return false;

	if (!receivePacket(socket, buf.data(), buf.size()))
		return false;

	// TODO: receive one-time server data
	return byte2msg(buf[0]) == MsgType::HELO_ACK;
}

static bool sendReadyAndWait(socket_t socket) {
	std::array<uint8_t, 1> buf = {};
	buf[0] = msg2byte(MsgType::READY);
	if (!sendPacket(socket, buf.data(), buf.size()))
		return false;

	if (!receivePacket(socket, buf.data(), buf.size()))
		return false;

	return byte2msg(buf[0]) == MsgType::READY;
}

static void keepaliveTask(socket_t socket, std::mutex& mtx, std::condition_variable& cv) {

	std::unique_lock<std::mutex> ulk{ mtx };
	const auto msg = msg2byte(MsgType::KEEPALIVE);

	while (true) {
		// Using a condition variable instead of sleep_for since we want to be able to interrupt it.
		const auto r = cv.wait_for(ulk, std::chrono::seconds{ cfg::CLIENT_KEEPALIVE_INTERVAL_SECONDS });
		if (r == std::cv_status::no_timeout) {
			info("keepalive task: interrupted");
			break;
		}
		if (!sendPacket(socket, &msg, 1))
			warn("Failed to send keepalive.");
	}
}

/* The logic here goes as follows:
 * - the client starts this thread via runLoop()
 * - the client waits for the handshake via await()
 * - this thread then waits to be notified by the client to proceed and send a ready msg;
 * - the client waits again for us to receive server's ready msg;
 * - as soon as we receive it this thread both notifies the client and starts the keepalive loop.
 */
void ClientReliableEndpoint::loopFunc() {

	if (!performHandshake(socket)) {
		err("Handshake failed");
		return;
	}
	cv.notify_one();

	std::mutex mtx;
	{
		std::unique_lock<std::mutex> ulk{ mtx };
		// Wait for the main thread to tell us to proceed
		cv.wait(ulk);
	}

	if (!sendReadyAndWait(socket)) {
		err("[ ERROR ] Sending or awaiting ready failed.");
		return;
	}
	cv.notify_one();

	// Spawn the keepalive routine
	std::thread keepaliveThread {
		keepaliveTask,
		socket,
		std::ref(mtx),
		std::ref(cv),
	};

	std::array<uint8_t, 1> buffer = {};
	connected = true;
	while (connected) {
		MsgType type;
		if (!receiveTCPMsg(socket, buffer.data(), buffer.size(), type)) {
			connected = false;
			break;
		}

		switch (type) {
		case MsgType::DISCONNECT:
			connected = false;
			break;
		default:
			break;
		}
	}

	info("Closing TCP connection.");
	cv.notify_all();
	if (keepaliveThread.joinable())
		keepaliveThread.join();
	info("Keepalive thread joined.");
}

void ClientReliableEndpoint::onClose() {
	cv.notify_all();
}
