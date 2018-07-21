#include "server_udp.hpp"
#include "camera.hpp"
#include "clock.hpp"
#include "config.hpp"
#include "defer.hpp"
#include "fps_counter.hpp"
#include "frame_utils.hpp"
#include "geom_update.hpp"
#include "logging.hpp"
#include "model.hpp"
#include "profile.hpp"
#include "server.hpp"
#include "server_appstage.hpp"
#include "udp_messages.hpp"
#include "udp_serialize.hpp"
#include "utils.hpp"
#include "xplatform.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <glm/gtx/string_cast.hpp>
#include <iostream>
#include <thread>
#include <vector>

using namespace logging;
using namespace std::chrono_literals;

// Delete ACKed messages from update queue
static void deleteAckedUpdates(std::vector<uint32_t>& acks, cf::hashmap<uint32_t, QueuedUpdate>& updates)
{
	for (auto ack : acks)
		updates.remove(ack, ack);

	acks.clear();
}

UdpActiveThread::UdpActiveThread(Server& server, Endpoint& ep)
	: server{ server }
	, ep{ ep }
{
	thread = std::thread{ &UdpActiveThread::udpActiveTask, this };
	xplatSetThreadName(thread, "UdpActive");
}

UdpActiveThread::~UdpActiveThread()
{
	server.toClient.updates.cv.notify_all();
	if (thread.joinable()) {
		info("Joining UdpActive thread...");
		thread.join();
		info("Joined UdpActive thread.");
	}
}

void UdpActiveThread::udpActiveTask()
{
	uint32_t packetGen = 0;

	std::array<uint8_t, cfg::PACKET_SIZE_BYTES> buffer = {};

	auto& updates = server.toClient.updates;

	FPSCounter fps{ "ActiveEP" };
	fps.start();
	fps.reportPeriod = 5;

	auto t = std::chrono::high_resolution_clock::now();
	std::size_t bytesPerSecond = 0;

	// Send datagrams to the client
	while (ep.connected) {
		const LimitFrameTime lft{ std::chrono::milliseconds{ 10 } };

		if (updates.size() == 0) {
			// Wait for updates
			std::unique_lock<std::mutex> ulk{ updates.mtx };
			updates.cv.wait(ulk, [this, &updates = server.toClient.updates]() {
				return !ep.connected || updates.size() > 0;
			});
			if (!ep.connected)
				break;
		}

		std::unique_lock<std::mutex> ulk{ updates.mtx };
		std::vector<QueuedUpdate> transitory = updates.transitory;
		updates.transitory.clear();
		ulk.unlock();

		auto offset = writeUdpHeader(buffer.data(), buffer.size(), packetGen);
		uberverbose("updates.size now = ", updates.size());

		// Send transitory updates
		for (auto it = transitory.begin(); it != transitory.end();) {
			if (!ep.connected)
				return;

			const auto& update = *it;
			const auto written = addUpdate(buffer.data(), buffer.size(), offset, update, server);

			if (written > 0) {
				// Packet was written into the buffer, erase it and go ahead
				offset += written;
				++it;
			} else {
				// Not enough room: send the packet
				sendPacket(ep.socket, buffer.data(), buffer.size());
				bytesPerSecond += buffer.size();

				// Start with a new packet
				writeUdpHeader(buffer.data(), buffer.size(), packetGen);
				offset = sizeof(UdpHeader);

				// Don't erase this element yet: retry in next iteration
			}
		}

		ulk.lock();
		// Remove all persistent updates which were acked by the client
		if (updates.persistent.size() > 0) {
			std::lock_guard<std::mutex> lock{ server.fromClient.acksReceivedMtx };
			deleteAckedUpdates(server.fromClient.acksReceived, updates.persistent);
		}

		if (updates.persistent.size() > 0) {
			verbose("sending ", updates.persistent.size(), " persistent updates");

			// Send persistent updates
			auto it = updates.persistent.iter_start();
			uint32_t ignoreKey;
			QueuedUpdate update;
			bool loop = updates.persistent.iter_next(it, ignoreKey, update);
			while (loop) {
				if (!ep.connected)
					return;

				// GEOM updates are currently the only ACKed ones
				assert(update.type == QueuedUpdate::Type::GEOM);
				const auto written = addUpdate(buffer.data(), buffer.size(), offset, update, server);

				if (written > 0) {
					offset += written;
					loop = updates.persistent.iter_next(it, ignoreKey, update);
				} else {
					// Not enough room: send the packet
					if (!sendPacket(ep.socket, buffer.data(), buffer.size()))
						break;
					bytesPerSecond += buffer.size();
					// info("pers: ", updates.persistent.size());

					writeUdpHeader(buffer.data(), buffer.size(), packetGen);
					offset = sizeof(UdpHeader);
				}
			}
		}
		ulk.unlock();

		if (offset > sizeof(UdpHeader)) {
			// Need to send the last packet
			sendPacket(ep.socket, buffer.data(), buffer.size());
			bytesPerSecond += buffer.size();
		}

		fps.addFrame();
		fps.report();

		auto tt = std::chrono::high_resolution_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(tt - t).count() >= 1) {
			t = tt;
			info("UDP bytes sent this second: ", bytesPerSecond);
			bytesPerSecond = 0;
		}

		++packetGen;
	}
}

////////////////////////////////////////

UdpPassiveThread::UdpPassiveThread(Server& server, Endpoint& ep)
	: server{ server }
	, ep{ ep }
{
	thread = std::thread{ &UdpPassiveThread::udpPassiveTask, this };
	xplatSetThreadName(thread, "UdpPassive");
}

UdpPassiveThread::~UdpPassiveThread()
{
	if (thread.joinable()) {
		info("Joining UdpPassive thread...");
		thread.join();
		info("Joined UdpPassive thread.");
	}
}

void UdpPassiveThread::udpPassiveTask()
{
	// Receive client ACKs to (some of) our UDP messages

	while (ep.connected) {
		std::array<uint8_t, cfg::PACKET_SIZE_BYTES> packetBuf = {};

		int bytesRead;
		if (!receivePacket(ep.socket, packetBuf.data(), packetBuf.size(), &bytesRead))
			continue;

		if (bytesRead != sizeof(AckPacket)) {
			warn("Read bogus packet from client (",
				bytesRead,
				" bytes instead of expected ",
				sizeof(AckPacket),
				")");
			continue;
		}

		const auto packet = reinterpret_cast<const AckPacket*>(packetBuf.data());
		if (packet->msgType != UdpMsgType::ACK) {
			warn("Read bogus packet from client (type is ",
				packet->msgType,
				" instead of ",
				UdpMsgType::ACK,
				")");
			continue;
		}

		if (server.fromClient.acksReceivedMtx.try_lock()) {
			constexpr unsigned MAX_ACKS = cfg::PACKET_SIZE_BYTES / sizeof(uint32_t);
			const unsigned nAcks = std::min(packet->nAcks, MAX_ACKS);
			for (unsigned i = 0; i < nAcks; ++i)
				server.fromClient.acksReceived.emplace_back(packet->acks[i]);
			server.fromClient.acksReceivedMtx.unlock();
		}
	}
}

/////////////////////////////////////////
