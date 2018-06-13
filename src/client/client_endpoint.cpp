#include "client_endpoint.hpp"
#include "camera.hpp"
#include "config.hpp"
#include "frame_data.hpp"
#include "frame_utils.hpp"
#include "logging.hpp"
#include "serialization.hpp"
#include "shared_resources.hpp"
#include "tcp_messages.hpp"
#include "udp_messages.hpp"
#include "units.hpp"
#include "utils.hpp"
#include "vertex.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace logging;
using namespace std::literals::chrono_literals;

static constexpr auto BUFSIZE = megabytes(16);

void ClientPassiveEndpoint::loopFunc()
{
	// This will be densely filled like this:
	// [chunk0.header|chunk0.payload|chunk1.header|chunk1.payload|...]
	buffer = new uint8_t[BUFSIZE];
	usedBufSize = 0;

	uint64_t packetGen = 0;

	// Receive datagrams and copy them into `buffer`.
	while (!terminated) {
		std::array<uint8_t, sizeof(udp::UpdatePacket)> packetBuf = {};
		if (!receivePacket(socket, packetBuf.data(), packetBuf.size()))
			continue;

		if (!validateUDPPacket(packetBuf.data(), packetGen))
			continue;

		const auto packet = reinterpret_cast<const udp::UpdatePacket*>(packetBuf.data());
		packetGen = packet->header.packetGen;

		const auto size = packet->header.size;
		assert(size <= packet->payload.size());
		// Just copy all the payload into `buffer` and let the main thread process it.
		{
			std::lock_guard<std::mutex> lock{ bufMtx };

			assert(usedBufSize + sizeof(uint32_t) + size < BUFSIZE);

			// Write packet size
			//*reinterpret_cast<uint32_t*>(buffer + usedBufSize) = size;
			// usedBufSize += sizeof(uint32_t);

			// Write packet data
			memcpy(buffer + usedBufSize, packet->payload.data(), size);
			usedBufSize += size;
		}

		if (usedBufSize >= BUFSIZE) {
			warn("Warning: buffer is being filled faster than it's consumed! Some data is being lost!");
			usedBufSize = 0;
		}
	}

	delete[] buffer;
}

std::size_t ClientPassiveEndpoint::retreive(uint8_t* outBuf, std::size_t outBufSize)
{
	if (outBufSize < usedBufSize) {
		std::stringstream ss;
		ss << "Buffer given to `retreive` is too small! (Given: " << (outBufSize / 1024) << " KiB, "
		   << "required: " << (usedBufSize / 1024) << " KiB)";
		throw std::runtime_error(ss.str());
	}

	std::lock_guard<std::mutex> lock{ bufMtx };
	memcpy(outBuf, buffer, usedBufSize);

	const auto written = usedBufSize;

	// Reset the buffer
	usedBufSize = 0;

	return written;
}

/////////////////////// Active EP
void ClientActiveEndpoint::loopFunc()
{
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

bool ClientReliableEndpoint::await(std::chrono::seconds timeout)
{
	std::mutex mtx;
	std::unique_lock<std::mutex> ulk{ mtx };
	return cv.wait_for(ulk, timeout) == std::cv_status::no_timeout;
}

static bool performHandshake(socket_t socket)
{

	std::array<uint8_t, 1> buf = {};

	// send HELO message
	if (!sendTCPMsg(socket, MsgType::HELO))
		return false;

	return expectTCPMsg(socket, buf.data(), 1, MsgType::HELO_ACK);
}

static bool sendReadyAndWait(socket_t socket)
{
	if (!sendTCPMsg(socket, MsgType::READY))
		return false;

	uint8_t buf;
	return expectTCPMsg(socket, &buf, 1, MsgType::READY);
}

static void keepaliveTask(socket_t socket, std::mutex& mtx, std::condition_variable& cv)
{

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
void ClientReliableEndpoint::loopFunc()
{

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
	std::thread keepaliveThread{
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

void ClientReliableEndpoint::onClose()
{
	cv.notify_all();
}

/** Reads header data from `buffer` and starts reading a texture. If more packets
 *  need to be read for the texture, receive them from `socket` until completion.
 *  Texture received is stored into `resources`.
 */
static bool receiveTexture(socket_t socket,
	const uint8_t* buffer,
	std::size_t bufsize,
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
	void* texdata = resources.allocator.alloc(expectedSize);
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
		info("Stored texture ", texName);
	}

	info("Received texture ", texName, ": ", texture.size, " B");
	if (gDebugLv >= LOGLV_VERBOSE) {
		dumpBytes(texture.data, texture.size);
	}

	return true;
}

/** Read a material out of `buffer` and store it in `resources` */
static bool receiveMaterial(const uint8_t* buffer,
	std::size_t bufsize,
	/* out */ ClientTmpResources& resources)
{
	assert(bufsize >= sizeof(shared::ResourcePacket<shared::Material>));
	static_assert(sizeof(StringId) == 4, "StringId size should be 4!");

	// [0] MsgType (1 B)
	// [1] material.name     (4 B)
	// [5] material.diffuse  (4 B)
	// [9] material.specular (4 B)
	// [13] material.normal  (4 B)
	const auto material = *reinterpret_cast<const shared::Material*>(buffer + 1);

	debug("received material: { name = ",
		material.name,
		", diff = ",
		material.diffuseTex,
		", spec = ",
		material.specularTex,
		", norm = ",
		material.normalTex,
		" }");

	if (resources.materials.count(material.name) > 0) {
		warn("Received the same material two times: ", material.name);
	} else {
		resources.materials[material.name] = material;
		info("Stored material ", material.name);
	}

	return true;
}

static bool receiveModel(socket_t socket,
	const uint8_t* buffer,
	std::size_t bufsize,
	/* out */ ClientTmpResources& resources)
{
	using shared::ResourcePacket;

	assert(bufsize >= sizeof(ResourcePacket<shared::Model>));

	// Parse header
	// [0]  MsgType    (1 B)
	// [1]  name       (4 B)
	// [5]  nVertices  (4 B)
	// [9]  nIndices   (4 B)
	// [13] nMaterials (1 B)
	// [14] nMeshes    (1 B)
	const auto header = *reinterpret_cast<const ResourcePacket<shared::Model>*>(buffer);
	const auto expectedSize = header.res.nMaterials * sizeof(StringId) + header.res.nMeshes * sizeof(shared::Mesh);

	if (expectedSize > cfg::MAX_MODEL_SIZE) {
		err("Model server sent is too big! (", expectedSize / 1024 / 1024., " MiB)");
		return false;
	}

	// Retreive payload [materials | meshes]

	void* payload = resources.allocator.alloc(expectedSize);
	if (!payload)
		return false;

	// Copy the first texture data embedded in the header packet into the texture memory area
	auto len = std::min(bufsize - sizeof(header), expectedSize);
	memcpy(payload, buffer + sizeof(header), len);

	// Receive remaining model information as raw data packets (if needed)
	auto processedSize = len;
	while (processedSize < expectedSize) {
		const auto remainingSize = expectedSize - processedSize;
		assert(remainingSize > 0);

		len = std::min(remainingSize, bufsize);

		if (!receivePacket(socket, reinterpret_cast<uint8_t*>(payload) + processedSize, len)) {
			resources.allocator.deallocLatest();
			return false;
		}

		processedSize += len;
	}

	if (processedSize != expectedSize) {
		warn("Processed more bytes than expected!");
	}

	ModelInfo model;
	model.name = header.res.name;
	model.nVertices = header.res.nVertices;
	model.nIndices = header.res.nIndices;

	model.materials.reserve(header.res.nMaterials);
	const auto materials = reinterpret_cast<const StringId*>(payload);
	for (unsigned i = 0; i < header.res.nMaterials; ++i)
		model.materials.emplace_back(materials[i]);

	model.meshes.reserve(header.res.nMeshes);
	const auto meshes = reinterpret_cast<const shared::Mesh*>(
		reinterpret_cast<uint8_t*>(payload) + header.res.nMaterials * sizeof(StringId));
	for (unsigned i = 0; i < header.res.nMeshes; ++i)
		model.meshes.emplace_back(meshes[i]);

	if (resources.models.count(model.name) != 0) {
		warn("Received the same model two times: ", model.name);
	} else {
		resources.models[model.name] = model;
		info("Stored model ", model.name);
	}

	debug("received model ", model.name, " (v=", model.nVertices, ", i=", model.nIndices, "):");
	if (gDebugLv >= LOGLV_DEBUG) {
		for (const auto& mat : model.materials)
			debug("material ", mat);

		for (const auto& mesh : model.meshes) {
			debug("mesh { off = ",
				mesh.offset,
				", len = ",
				mesh.len,
				", mat = ",
				mesh.materialId,
				" (",
				mesh.materialId >= 0 ? model.materials[mesh.materialId] : SID_NONE,
				") }");
		}
	}

	return true;
}

bool ClientReliableEndpoint::receiveOneTimeData()
{
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

			if (!receiveModel(socket, buffer.data(), buffer.size(), *resources)) {
				err("Failed to receive model");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, MsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}
		} break;

		default:
			err("Invalid data type: ", incomingDataType, " (", unsigned(incomingDataType), ")");
			// Retry: maybe it was garbage from the previous sending
			// return false;
		}
	}
}

bool ClientReliableEndpoint::disconnect()
{
	return sendTCPMsg(socket, MsgType::DISCONNECT);
}
