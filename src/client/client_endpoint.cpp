#include "client_endpoint.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <array>
#include <cstring>
#include "frame_data.hpp"
#include "config.hpp"
#include "vertex.hpp"
#include "camera.hpp"
#include "serialization.hpp"
#include "frame_utils.hpp"
#include "tcp_messages.hpp"
#include "shared_resources.hpp"
#include "logging.hpp"
#include "utils.hpp"

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
	if (!sendTCPMsg(socket, MsgType::HELO))
		return false;

	return expectTCPMsg(socket, buf.data(), 1, MsgType::HELO_ACK);
}

static bool sendReadyAndWait(socket_t socket) {
	if (!sendTCPMsg(socket, MsgType::READY))
		return false;

	uint8_t buf;
	return expectTCPMsg(socket, &buf, 1, MsgType::READY);
}

static void keepaliveTask(socket_t socket, std::mutex& mtx, std::condition_variable& cv) {

	std::unique_lock<std::mutex> ulk{ mtx };

	while (true) {
		// Using a condition variable instead of sleep_for since we want to be able to interrupt it.
		const auto r = cv.wait_for(ulk, std::chrono::seconds{ cfg::CLIENT_KEEPALIVE_INTERVAL_SECONDS });
		if (r == std::cv_status::no_timeout) {
			info("keepalive task: interrupted");
			break;
		}
		if (!sendTCPMsg(socket, MsgType::KEEPALIVE))
			warn("Failed to send keepalive.");
	}
}

/* The logic here goes as follows:
 * - the main thread starts this thread via runLoop()
 * - the main thread waits for the handshake via await()
 * - this thread then waits to be notified by the main thread to proceed and receive the data;
 * - once the data is received, we notify the main thread and wait;
 * - the client awaits us and we send a READY msg;
 * - the client waits again for us to receive server's ready msg;
 * - as soon as we receive it this thread both notifies the client and starts the keepalive loop.
 */
void ClientReliableEndpoint::loopFunc() {

	// -> HELO / <- HELO-ACK
	if (!performHandshake(socket)) {
		err("Handshake failed");
		return;
	}

	{
		uint8_t buffer;
		if (!expectTCPMsg(socket, &buffer, 1, MsgType::START_DATA_EXCHANGE)) {
			err("Expecting START_DATA_EXCHANGE but didn't receive it.");
			return;
		}
	}

	cv.notify_one();

	std::mutex mtx;
	{
		std::unique_lock<std::mutex> ulk{ mtx };
		// Wait for the main thread to tell us to proceed
		cv.wait(ulk);
	}

	// Ready to receive one-time data
	if (!sendTCPMsg(socket, MsgType::DATA_EXCHANGE_ACK))
		return;

	info("Waiting for one-time data...");
	if (!receiveOneTimeData()) {
		err("Error receiving one time data.");
		return;
	}
	cv.notify_one();

	{
		std::unique_lock<std::mutex> ulk{ mtx };
		// Wait for the main thread to process the received assets
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

static bool receiveTexture(socket_t socket,
		std::array<uint8_t, cfg::PACKET_SIZE_BYTES>& buffer,
		/* out */ shared::Texture& texture,
		/* out */ StringId& texName)
{
	uint64_t expectedSize = 0;
	uint64_t processedSize = 0;

	// Parse header
	// [0]  msgType     (1 B)
	// [1]  size        (8 B)
	// [9]  head.name   (4 B)
	// [13] head.format (1 B)
	expectedSize = *reinterpret_cast<uint64_t*>(buffer.data() + 1);

	if (expectedSize > cfg::MAX_TEXTURE_SIZE) {
		err("Texture server sent is too big! (", expectedSize / 1024 / 1024., " MiB)");
		return false;
	}

	texName = *reinterpret_cast<StringId*>(buffer.data() + 9);

	auto format = static_cast<shared::TextureFormat>(buffer[13]);
	assert(static_cast<uint8_t>(format) < static_cast<uint8_t>(shared::TextureFormat::UNKNOWN));

	auto texdata = new uint8_t[expectedSize];

	constexpr auto HEADER_SIZE = 14;

	auto len = std::min(buffer.size() - HEADER_SIZE, expectedSize);
	memcpy(texdata, buffer.data() + HEADER_SIZE, len);
	processedSize += len;

	// Receive texture data
	while (processedSize < expectedSize) {
		const auto remainingSize = expectedSize - processedSize;
		assert(remainingSize > 0);

		len = std::min(remainingSize, buffer.size());

		if (!receivePacket(socket, buffer.data(), len))
			return false;

		memcpy(texdata + processedSize, buffer.data(), len);
		processedSize += len;
	}

	if (processedSize != expectedSize) {
		warn("Processed more bytes than expected!");
	}

	texture.size = expectedSize;
	texture.data = texdata;
	texture.format = format;

	return true;
}

bool ClientReliableEndpoint::receiveOneTimeData() {
	std::array<uint8_t, cfg::PACKET_SIZE_BYTES> buffer;

	assert(resources != nullptr);

	// Receive data
	while (true) {
		MsgType incomingDataType = MsgType::UNKNOWN;

		if (!receiveTCPMsg(socket, buffer.data(), buffer.size(), incomingDataType)) {
			err("Error receiving data packet.");
			return false;
		}

		switch (incomingDataType) {

		case MsgType::DISCONNECT:
			return false;

		case MsgType::END_DATA_EXCHANGE:
			return true;

		case MsgType::DATA_TYPE_TEXTURE: {

			shared::Texture texture;
			StringId texName;

			if (!receiveTexture(socket, buffer, texture, texName)) {
				err("Failed to receive texture.");
				return false;
			}
			info("Received texture ", texName, ": ", texture.size, " B");
			if (gDebugLv >= LOGLV_VERBOSE) {
				dumpBytes(texture.data, texture.size);
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, MsgType::DATA_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			// Save the texture in client resources
			if (resources->textures.count(texName) > 0) {
				warn("Received the same texture two times: ", texName);
			} else {
				resources->storeTexture(texName, texture);
			}
			delete [] reinterpret_cast<uint8_t*>(texture.data);

		} break;

		default:
			err("Invalid data type: ", incomingDataType, " (", unsigned(incomingDataType), ")");
			// Retry: maybe it was garbage from the previous sending
			//return false;
		}
	}
}
