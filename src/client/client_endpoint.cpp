// TODO: ensure no spurious wakeup
#include "client_endpoint.hpp"
#include "camera.hpp"
#include "config.hpp"
#include "frame_data.hpp"
#include "frame_utils.hpp"
#include "logging.hpp"
#include "shared_resources.hpp"
#include "tcp_deserialize.hpp"
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

static constexpr auto BUFSIZE = megabytes(256);

void ClientPassiveEndpoint::loopFunc()
{
	// This will be densely filled like this:
	// [chunk0.type|chunk0.header|chunk0.payload|chunk1.type|chunk1.header|chunk1.payload|...]
	buffer = new uint8_t[BUFSIZE];
	usedBufSize = 0;

	uint32_t packetGen = 0;

	// Receive datagrams and copy them into `buffer`.
	while (!terminated) {
		std::array<uint8_t, cfg::PACKET_SIZE_BYTES> packetBuf = {};

		if (!receivePacket(socket, packetBuf.data(), packetBuf.size()))
			continue;

		if (!validateUDPPacket(packetBuf.data(), packetGen))
			continue;

		const auto packet = reinterpret_cast<const UdpPacket*>(packetBuf.data());
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
	// Send ACKs
	while (!terminated) {
		std::unique_lock<std::mutex> ulk{ acks.mtx };
		if (acks.list.size() == 0) {
			// Wait for ACKs to send
			acks.cv.wait(ulk, [this]() { return terminated || acks.list.size() > 0; });
		}

		AckPacket packet;
		packet.msgType = UdpMsgType::ACK;
		packet.nAcks = 0;

		for (auto ack : acks.list) {
			packet.acks[packet.nAcks] = ack;
			packet.nAcks++;
			if (packet.nAcks == packet.acks.size()) {
				// Packet is full: send
				sendPacket(socket, reinterpret_cast<const uint8_t*>(&packet), sizeof(AckPacket));
				// info("Sent ", packet.nAcks, " acks: ", listToString(acks.list));
				packet.nAcks = 0;
			}
		}
		if (packet.nAcks > 0) {
			sendPacket(socket, reinterpret_cast<const uint8_t*>(&packet), sizeof(AckPacket));
			info("Sent ", packet.nAcks, " acks");
		}

		acks.list.clear();
	}
}

void ClientActiveEndpoint::onClose()
{
	acks.cv.notify_all();
}

/////////////////////// ReliableEP

bool ClientReliableEndpoint::performHandshake()
{
	// send HELO message
	if (!sendTCPMsg(socket, TcpMsgType::HELO))
		return false;

	uint8_t buffer;
	return expectTCPMsg(socket, &buffer, 1, TcpMsgType::HELO_ACK);
}

bool ClientReliableEndpoint::expectStartResourceExchange()
{
	uint8_t buffer;
	return expectTCPMsg(socket, &buffer, 1, TcpMsgType::START_RSRC_EXCHANGE);
}

bool ClientReliableEndpoint::sendReadyAndWait()
{
	if (!sendTCPMsg(socket, TcpMsgType::READY))
		return false;

	uint8_t buf;
	return expectTCPMsg(socket, &buf, 1, TcpMsgType::READY);
}

bool ClientReliableEndpoint::sendRsrcExchangeAck()
{
	return sendTCPMsg(socket, TcpMsgType::RSRC_EXCHANGE_ACK);
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
		if (!sendTCPMsg(socket, TcpMsgType::KEEPALIVE))
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
		TcpMsgType type;
		if (!receiveTCPMsg(socket, buffer.data(), buffer.size(), type)) {
			connected = false;
			break;
		}

		switch (type) {
		case TcpMsgType::DISCONNECT:
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
		auto incomingDataType = TcpMsgType::UNKNOWN;

		if (!receiveTCPMsg(socket, buffer.data(), buffer.size(), incomingDataType)) {
			err("Error receiving data packet.");
			return false;
		}

		switch (incomingDataType) {

		case TcpMsgType::DISCONNECT:
			return false;

		case TcpMsgType::END_RSRC_EXCHANGE:
			return true;

		case TcpMsgType::RSRC_TYPE_TEXTURE:

			if (!receiveTexture(socket, buffer.data(), buffer.size(), resources)) {
				err("Failed to receive texture.");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, TcpMsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			break;

		case TcpMsgType::RSRC_TYPE_MATERIAL:

			if (!receiveMaterial(buffer.data(), buffer.size(), resources)) {
				err("Failed to receive material");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, TcpMsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			break;

		case TcpMsgType::RSRC_TYPE_MODEL:

			if (!receiveModel(socket, buffer.data(), buffer.size(), resources)) {
				err("Failed to receive model");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, TcpMsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			break;

		case TcpMsgType::RSRC_TYPE_POINT_LIGHT:
			if (!receivePointLight(buffer.data(), buffer.size(), resources)) {
				err("Failed to receive point light");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(socket, TcpMsgType::RSRC_EXCHANGE_ACK)) {
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
	return sendTCPMsg(socket, TcpMsgType::DISCONNECT);
}
