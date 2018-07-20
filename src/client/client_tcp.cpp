#include "client_tcp.hpp"
#include "config.hpp"
#include "logging.hpp"
#include "shared_resources.hpp"
#include "tcp_deserialize.hpp"
#include "tcp_messages.hpp"
#include "xplatform.hpp"
#include <array>
#include <cstddef>

using namespace logging;

bool tcp_performHandshake(socket_t socket)
{
	// send HELO message
	if (!sendTCPMsg(socket, TcpMsgType::HELO))
		return false;

	uint8_t buffer;
	return expectTCPMsg(socket, &buffer, 1, TcpMsgType::HELO_ACK);
}

bool tcp_expectStartResourceExchange(socket_t socket)
{
	uint8_t buffer;
	return expectTCPMsg(socket, &buffer, 1, TcpMsgType::START_RSRC_EXCHANGE);
}

bool tcp_sendReadyAndWait(socket_t socket)
{
	if (!sendTCPMsg(socket, TcpMsgType::READY))
		return false;

	uint8_t buf;
	return expectTCPMsg(socket, &buf, 1, TcpMsgType::READY);
}

bool tcp_sendRsrcExchangeAck(socket_t socket)
{
	return sendTCPMsg(socket, TcpMsgType::RSRC_EXCHANGE_ACK);
}

////////////////////

static void keepaliveTask(const Endpoint& ep, std::condition_variable& cv)
{
	std::mutex mtx;
	while (ep.connected) {
		std::unique_lock<std::mutex> ulk{ mtx };

		// Using a condition variable instead of sleep_for since we want to be able to interrupt it.
		const auto r =
			cv.wait_for(ulk, std::chrono::seconds{ 5 });   // cfg::CLIENT_KEEPALIVE_INTERVAL_SECONDS });
		if (r == std::cv_status::no_timeout && !ep.connected) {
			info("keepalive task: interrupted");
			break;
		}
		if (!sendTCPMsg(ep.socket, TcpMsgType::KEEPALIVE))
			warn("Failed to send keepalive.");
	}
}

KeepaliveThread::KeepaliveThread(const Endpoint& ep)
{
	thread = std::thread{ keepaliveTask, std::cref(ep), std::ref(cv) };
	xplatSetThreadName(thread, "Keepalive");
}

KeepaliveThread::~KeepaliveThread()
{
	cv.notify_all();
	if (thread.joinable()) {
		info("Joining keepalive thread...");
		thread.join();
		info("Joined keepalive thread.");
	}
}

/////////////////////

void TcpMsgThread::tcpMsgTask()
{
	std::array<uint8_t, 1> buffer = {};
	debug("tcpMsgTask: starting.");
	while (running) {
		TcpMsgType type;
		if (!receiveTCPMsg(ep.socket, buffer.data(), buffer.size(), type)) {
			running = false;
			break;
		}

		switch (type) {
		case TcpMsgType::DISCONNECT:
			info("Received DISCONNECT");
			running = false;
			break;
		case TcpMsgType::START_RSRC_EXCHANGE:
			performResourceExchange();
			break;
		default:
			break;
		}
	}
	info("tcpMsgTask: exiting.");
	closeEndpoint(ep);
}

TcpMsgThread::TcpMsgThread(Endpoint& ep)
	: ep{ ep }
	, resources{ megabytes(128) }
{
	thread = std::thread{ &TcpMsgThread::tcpMsgTask, this };
	xplatSetThreadName(thread, "TcpReceive");
}

TcpMsgThread::~TcpMsgThread()
{
	if (thread.joinable()) {
		info("Joining tcp msg thread...");
		thread.join();
		info("Joined tcp msg thread.");
	}
}

void TcpMsgThread::performResourceExchange()
{
	// If previous resources were there, clear them unless the client hasn't
	// retreived them yet.
	std::lock_guard<std::mutex> lock{ resourcesMtx };

	if (!resourcesAvailable)
		resources.clear();

	sendTCPMsg(ep.socket, TcpMsgType::RSRC_EXCHANGE_ACK);

	if (receiveOneTimeData()) {
		resourcesAvailable = true;
	}
}

bool TcpMsgThread::tryLockResources()
{
	if (resourcesAvailable) {
		return resourcesMtx.try_lock();
	}
	return false;
}

void TcpMsgThread::releaseResources()
{
	resourcesAvailable = false;
	resourcesMtx.unlock();
}

bool TcpMsgThread::receiveOneTimeData()
{
	std::array<uint8_t, cfg::PACKET_SIZE_BYTES> buffer;

	// Receive data
	while (ep.connected) {
		auto incomingDataType = TcpMsgType::UNKNOWN;

		if (!receiveTCPMsg(ep.socket, buffer.data(), buffer.size(), incomingDataType)) {
			err("Error receiving data packet.");
			return false;
		}

		switch (incomingDataType) {

		case TcpMsgType::DISCONNECT:
			return false;

		case TcpMsgType::END_RSRC_EXCHANGE:
			return true;

		case TcpMsgType::RSRC_TYPE_TEXTURE:

			if (!receiveTexture(ep.socket, buffer.data(), buffer.size(), resources)) {
				err("Failed to receive texture.");
				return false;
			}

			// All green, send ACK
			if (!sendTCPMsg(ep.socket, TcpMsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			break;

		case TcpMsgType::RSRC_TYPE_MATERIAL:

			if (!receiveMaterial(buffer.data(), buffer.size(), resources)) {
				err("Failed to receive material");
				return false;
			}

			if (!sendTCPMsg(ep.socket, TcpMsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			break;

		case TcpMsgType::RSRC_TYPE_MODEL:

			if (!receiveModel(ep.socket, buffer.data(), buffer.size(), resources)) {
				err("Failed to receive model");
				return false;
			}

			if (!sendTCPMsg(ep.socket, TcpMsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			break;

		case TcpMsgType::RSRC_TYPE_POINT_LIGHT:
			if (!receivePointLight(buffer.data(), buffer.size(), resources)) {
				err("Failed to receive point light");
				return false;
			}

			if (!sendTCPMsg(ep.socket, TcpMsgType::RSRC_EXCHANGE_ACK)) {
				err("Failed to send ACK");
				return false;
			}

			break;

		case TcpMsgType::RSRC_TYPE_SHADER:
			if (!receiveShader(ep.socket, buffer.data(), buffer.size(), resources)) {
				err("Failed to receive shader");
				return false;
			}

			if (!sendTCPMsg(ep.socket, TcpMsgType::RSRC_EXCHANGE_ACK)) {
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

	return false;
}
