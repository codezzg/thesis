// TODO: ensure no spurious wakeup
#include "client_endpoint.hpp"
#include "camera.hpp"
#include "config.hpp"
#include "frame_data.hpp"
#include "frame_utils.hpp"
#include "logging.hpp"
#include "rsrc_recv.hpp"
#include "shared_resources.hpp"
#include "tcp_messages.hpp"
#include "udp_messages.hpp"
#include "units.hpp"
#include "utils.hpp"
#include "vertex.hpp"
#include <algorithm>
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
		if (size > packet->payload.size()) {
			err("Packet size is ", size, " > ", packet->payload.size(), "!");
			continue;
		}

		// Just copy all the payload into `buffer` and let the main thread process it.
		{
			std::lock_guard<std::mutex> lock{ bufMtx };

			assert(usedBufSize + size < BUFSIZE);

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
		if (camera) {
			shared::Camera shCamera;
			shCamera.x = camera->position.x;
			shCamera.y = camera->position.y;
			shCamera.z = camera->position.z;
			shCamera.yaw = camera->yaw;
			shCamera.pitch = camera->pitch;
			static_assert(FrameData().payload.size() >= sizeof(shared::Camera),
				"Camera does not fit in payload!");
			memcpy(data.payload.data(), &shCamera, sizeof(shared::Camera));
		}

		sendPacket(socket, reinterpret_cast<const uint8_t*>(&data), sizeof(FrameData));

		++frameId;
		delay = lft.getFrameDelay();
	}
}

/////////////////////// ReliableEP

bool ClientReliableEndpoint::performHandshake()
{
	// send HELO message
	if (!sendTCPMsg(socket, MsgType::HELO))
		return false;

	uint8_t buffer;
	return expectTCPMsg(socket, &buffer, 1, MsgType::HELO_ACK);
}

bool ClientReliableEndpoint::expectStartResourceExchange()
{
	uint8_t buffer;
	return expectTCPMsg(socket, &buffer, 1, MsgType::START_RSRC_EXCHANGE);
}

bool ClientReliableEndpoint::sendReadyAndWait()
{
	if (!sendTCPMsg(socket, MsgType::READY))
		return false;

	uint8_t buf;
	return expectTCPMsg(socket, &buf, 1, MsgType::READY);
}

bool ClientReliableEndpoint::sendRsrcExchangeAck()
{
	return sendTCPMsg(socket, MsgType::RSRC_EXCHANGE_ACK);
}

static void keepaliveTask(socket_t socket, std::condition_variable& cv)
{
	std::mutex mtx;
	while (true) {
		std::unique_lock<std::mutex> ulk{ mtx };

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

void ClientReliableEndpoint::loopFunc()
{
	// Spawn the keepalive routine
	std::thread keepaliveThread{
		keepaliveTask,
		socket,
		std::ref(keepaliveCv),
	};

	std::array<uint8_t, 1> buffer = {};
	debug("ep :: Starting msg receiving loop");
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
	keepaliveCv.notify_all();
	if (keepaliveThread.joinable())
		keepaliveThread.join();
	info("Keepalive thread joined.");
}

void ClientReliableEndpoint::onClose()
{
	keepaliveCv.notify_all();
}

bool ClientReliableEndpoint::receiveOneTimeData(ClientTmpResources& resources)
{
	std::array<uint8_t, cfg::PACKET_SIZE_BYTES> buffer;

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

		case MsgType::RSRC_TYPE_TEXTURE:

			if (!receiveTexture(socket, buffer.data(), buffer.size(), resources)) {
				err("Failed to receive texture.");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, MsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			break;

		case MsgType::RSRC_TYPE_MATERIAL:

			if (!receiveMaterial(buffer.data(), buffer.size(), resources)) {
				err("Failed to receive material");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, MsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			break;

		case MsgType::RSRC_TYPE_MODEL:

			if (!receiveModel(socket, buffer.data(), buffer.size(), resources)) {
				err("Failed to receive model");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, MsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			break;

		case MsgType::RSRC_TYPE_POINT_LIGHT:
			if (!receivePointLight(buffer.data(), buffer.size(), resources)) {
				err("Failed to receive point light");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, MsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			break;

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
