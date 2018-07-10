#pragma once

#include "client_resources.hpp"
#include "endpoint.hpp"
#include "endpoint_xplatform.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

bool tcp_performHandshake(socket_t sock);
bool tcp_expectStartResourceExchange(socket_t sock);
bool tcp_sendRsrcExchangeAck(socket_t sock);
bool tcp_sendReadyAndWait(socket_t sock);

class KeepaliveThread {
	std::thread thread;

public:
	std::condition_variable cv;

	explicit KeepaliveThread(socket_t s);
	~KeepaliveThread();
};

class TcpMsgThread {
	Endpoint& ep;

	std::thread thread;
	bool running = true;

	/** Fills `resources` with the data incoming from the server until END_RSRC_EXCHANGE is received. */
	bool receiveOneTimeData();
	void performResourceExchange();

	void tcpMsgTask();

public:
	ClientTmpResources resources;
	std::mutex resourcesMtx;
	bool resourcesAvailable = false;

	explicit TcpMsgThread(Endpoint& ep);
	~TcpMsgThread();

	bool tryLockResources();
	const ClientTmpResources* retreiveResources() const { return &resources; }
	void releaseResources();
};
