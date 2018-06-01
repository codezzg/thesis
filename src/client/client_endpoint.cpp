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
#include "units.hpp"

using namespace logging;
using namespace std::literals::chrono_literals;

static constexpr auto BUFSIZE = megabytes(16);

void ClientPassiveEndpoint::loopFunc() {

	// This will be filled like this:
	// [vertices|indices]
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
		}

		uint8_t *payload = packet->payload.data();
		auto payloadLen = packet->payload.size();

		//dumpPacket("client.dump", *packet);

		// Compute the offset to insert data at
		const size_t offset = packet->header.packetId * packet->payload.size();

		// Insert data into the buffer
		memcpy(backBuffer + offset, payload, payloadLen);

		nBytesReceived += payloadLen;
		//std::cerr << "payload len = " << payloadLen << "\n";
		verbose("Bytes received: ", nBytesReceived, " / ",
				nVertices * sizeof(Vertex) + nIndices * sizeof(Index));
		bufferFilled = nBytesReceived >= (nVertices * sizeof(Vertex) + nIndices * sizeof(Index));

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
	phead.nVertices = nVertices;
	phead.nIndices = nIndices;
	{
		std::lock_guard<std::mutex> lock{ bufMtx };
		memcpy(outVBuf, buffer, nVertices * sizeof(Vertex));
		memcpy(outIBuf, buffer + nVertices * sizeof(Vertex), nIndices * sizeof(Index));
	}
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
		if (!expectTCPMsg(socket, &buffer, 1, MsgType::START_RSRC_EXCHANGE)) {
			err("Expecting START_RSRC_EXCHANGE but didn't receive it.");
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
	if (!sendTCPMsg(socket, MsgType::RSRC_EXCHANGE_ACK))
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

/** Reads header data from `buffer` and starts reading a texture. If more packets
 *  need to be read for the texture, receive them from `socket` until completion.
 *  Texture received is stored into `resources`.
 */
static bool receiveTexture(socket_t socket, const uint8_t *buffer, std::size_t bufsize,
		/* out */ ClientTmpResources& resources)
{
	// Parse header
	// [0] msgType    (1 B)
	// [1] tex.name   (4 B)
	// [5] tex.format (1 B)
	// [6] tex.size   (8 B)
	const auto expectedSize = *reinterpret_cast<const uint64_t*>(buffer + 6);

	if (expectedSize > cfg::MAX_TEXTURE_SIZE) {
		err("Texture server sent is too big! (", expectedSize / 1024 / 1024., " MiB)");
		return false;
	}

	const auto texName = *reinterpret_cast<const StringId*>(buffer + 1);

	auto format = static_cast<shared::TextureFormat>(buffer[5]);
	assert(static_cast<uint8_t>(format) < static_cast<uint8_t>(shared::TextureFormat::UNKNOWN));

	// Retreive payload

	/** Obtain the memory to store the texture data in */
	void *texdata = resources.allocator.alloc(expectedSize);
	if (!texdata)
		return false;

	constexpr auto HEADER_SIZE = 14;

	// Copy the first texture data embedded in the header packet into the texture memory area
	auto len = std::min(bufsize - HEADER_SIZE, expectedSize);
	memcpy(texdata, buffer + HEADER_SIZE, len);

	// Receive remaining texture data as raw data packets (if needed)
	auto processedSize = len;
	while (processedSize < expectedSize) {
		const auto remainingSize = expectedSize - processedSize;
		assert(remainingSize > 0);

		len = std::min(remainingSize, bufsize);

		// Receive the data directly into the texture memory area (avoids a memcpy from the buffer)
		if (!receivePacket(socket, reinterpret_cast<uint8_t*>(texdata) + processedSize, len)) {
			resources.allocator.deallocLatest();
			return false;
		}

		processedSize += len;
	}

	if (processedSize != expectedSize) {
		warn("Processed more bytes than expected!");
	}

	shared::Texture texture;
	texture.size = expectedSize;
	texture.data = texdata;
	texture.format = format;

	if (resources.textures.count(texName) > 0) {
		warn("Received the same texture two times: ", texName);
	} else {
		resources.textures[texName] = texture;
	}

	info("Received texture ", texName, ": ", texture.size, " B");
	if (gDebugLv >= LOGLV_VERBOSE) {
		dumpBytes(texture.data, texture.size);
	}

	return true;
}

/** Read a material out of `buffer` and store it in `resources` */
static bool receiveMaterial(const uint8_t *buffer, std::size_t bufsize,
		/* out */ ClientTmpResources& resources)
{
	assert(bufsize >= sizeof(shared::ResourcePacket<shared::Material>));
	static_assert(sizeof(StringId) == 4, "StringId size should be 4!");

	// [0] MsgType (1 B)
	// [1] material.name     (4 B)
	// [5] material.diffuse  (4 B)
	// [9] material.specular (4 B)
	const auto material = *reinterpret_cast<const shared::Material*>(buffer + 1);

	debug("received material: { name = ", material.name,
			", diff = ", material.diffuseTex, ", spec = ", material.specularTex, " }");

	if (resources.materials.count(material.name) > 0) {
		warn("Received the same material two times: ", material.name);
	} else {
		resources.materials[material.name] = material;
		info("Stored material ", material.name);
	}

	return true;
}

static bool receiveModel(uint8_t *buffer, std::size_t bufsize,
		/* out */ shared::Model& model)
{
	/*
	constexpr auto sizeOfPrelude = sizeof(MsgType) + sizeof(uint64_t) + 2 * sizeof(uint8_t);
	assert(bufsize >= sizeOfPrelude);

	// [0] MsgType    (1 B)
	// [1] nMaterials (1 B)
	// [2] nMeshes    (1 B)
	// [3] materialIds (nMaterials * 4 B)
	// [?] meshes      (nMeshes * 10 B)
	const auto nMaterials = buffer[1];
	const auto nMeshes = buffer[2];

	model.nMaterials = nMaterials;
	model.nMeshes = nMeshes;
	//model.payload = new uint8_t[nMaterials * sizeof(StringId) + nMeshes * sizeof(shared::Mesh)];

	memcpy(reinterpret_cast<void*>(model.payload), buffer + 11,
			nMaterials * sizeof(StringId) + nMeshes * sizeof(shared::Mesh));

	debug("received model:");
	if (gDebugLv >= LOGLV_DEBUG) {
		auto mats = reinterpret_cast<const StringId*>(model.payload);
		for (unsigned i = 0; i < model.nMaterials; ++i)
			debug("material ", mats[i]);

		auto meshes = reinterpret_cast<const shared::Mesh*>(model.payload + nMaterials * sizeof(StringId));
		for (unsigned i = 0; i < model.nMeshes; ++i) {
			const auto& mesh = meshes[i];
			debug("mesh { off = ", mesh.offset, ", len = ", mesh.len, ", mat = ", mesh.materialId, " }");
		}
	}*/

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

		case MsgType::END_RSRC_EXCHANGE:
			return true;

		case MsgType::RSRC_TYPE_TEXTURE: {

			if (!receiveTexture(socket, buffer.data(), buffer.size(), *resources)) {
				err("Failed to receive texture.");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, MsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

		} break;

		case MsgType::RSRC_TYPE_MATERIAL: {

			if (!receiveMaterial(buffer.data(), buffer.size(), *resources)) {
				err("Failed to receive material");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, MsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

		} break;

		case MsgType::RSRC_TYPE_MODEL: {

			shared::Model model;
			if (!receiveModel(buffer.data(), buffer.size(), model)) {
				err("Failed to receive model");
				return false;
			}
		} break;

		default:
			err("Invalid data type: ", incomingDataType, " (", unsigned(incomingDataType), ")");
			// Retry: maybe it was garbage from the previous sending
			//return false;
		}
	}
}
