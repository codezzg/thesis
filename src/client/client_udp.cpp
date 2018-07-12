#include "client_udp.hpp"
#include "camera.hpp"
#include "config.hpp"
#include "frame_data.hpp"
#include "frame_utils.hpp"
#include "logging.hpp"
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

static constexpr auto BUFSIZE = megabytes(128);

void UdpPassiveThread::udpPassiveTask()
{
	// This will be densely filled like this:
	// [chunk0.type|chunk0.header|chunk0.payload|chunk1.type|chunk1.header|chunk1.payload|...]
	uint32_t packetGen = 0;

	// Receive datagrams and copy them into `buffer`.
	while (ep.connected) {
		std::array<uint8_t, cfg::PACKET_SIZE_BYTES> packetBuf = {};

		if (!receivePacket(ep.socket, packetBuf.data(), packetBuf.size()))
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

			if (usedBufSize + size >= BUFSIZE) {
				warn("Warning: buffer is being filled faster than it's consumed! Some data is being lost!");
				usedBufSize = 0;
				continue;
			}

			// Write packet data
			memcpy(buffer + usedBufSize, packet->payload.data(), size);
			usedBufSize += size;
		}
	}
}

UdpPassiveThread::UdpPassiveThread(Endpoint& ep)
	: ep{ ep }
{
	buffer = new uint8_t[BUFSIZE];
	usedBufSize = 0;
	thread = std::thread{ &UdpPassiveThread::udpPassiveTask, this };
}

UdpPassiveThread::~UdpPassiveThread()
{
	if (thread.joinable()) {
		info("Joining UDP passive thread...");
		thread.join();
		info("Joined UDP passive thread.");
	}
	delete[] buffer;
}

std::size_t UdpPassiveThread::retreive(uint8_t* outBuf, std::size_t outBufSize)
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
void UdpActiveThread::udpActiveTask(Endpoint& ep)
{
	// Send ACKs
	while (ep.connected) {
		std::unique_lock<std::mutex> ulk{ acks.mtx };
		if (acks.list.size() == 0) {
			// Wait for ACKs to send
			acks.cv.wait(ulk, [&]() { return !ep.connected || acks.list.size() > 0; });
		}

		AckPacket packet;
		packet.msgType = UdpMsgType::ACK;
		packet.nAcks = 0;

		for (auto ack : acks.list) {
			packet.acks[packet.nAcks] = ack;
			packet.nAcks++;
			if (packet.nAcks == packet.acks.size()) {
				// Packet is full: send
				sendPacket(ep.socket, reinterpret_cast<const uint8_t*>(&packet), sizeof(AckPacket));
				// info("Sent ", packet.nAcks, " acks: ", listToString(acks.list));
				packet.nAcks = 0;
			}
		}
		if (packet.nAcks > 0) {
			sendPacket(ep.socket, reinterpret_cast<const uint8_t*>(&packet), sizeof(AckPacket));
			verbose("Sent ", packet.nAcks, " acks");
		}

		acks.list.clear();
	}
}

UdpActiveThread::UdpActiveThread(Endpoint& ep)
{
	thread = std::thread{ &UdpActiveThread::udpActiveTask, this, std::ref(ep) };
}

UdpActiveThread::~UdpActiveThread()
{
	acks.cv.notify_all();
	if (thread.joinable()) {
		info("Joining UDP active thread...");
		thread.join();
		info("Joined UDP active thread.");
	}
}
