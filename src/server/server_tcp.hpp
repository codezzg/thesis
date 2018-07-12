#pragma once

#include "endpoint.hpp"
#include "server_resources.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

struct Server;

/** This class implements a reliable connection server endpoint which handles the server-side
 *  reliable communication channel.
 *  It's used to perform initial handshake and to send reliable messages to the client.
 */
class TcpActiveThread {

	std::thread thread;

	Server& server;
	Endpoint& ep;

	/** Starts the UDP + keepalive endpoints towards client */
	void connectToClient(socket_t clientSocket, const char* clientAddr);

	/** The TCP main loop */
	bool msgLoop(socket_t clientSocket, sockaddr_in clientAddr);

	/** Sends all the one-time data the client needs. */
	bool sendOneTimeData(socket_t clientSocket);

	void dropClient(socket_t clientSocket);

	void tcpActiveTask();

public:
	ResourceBatch resourcesToSend;
	std::mutex mtx;
	std::condition_variable cv;

	explicit TcpActiveThread(Server& server, Endpoint& ep);
	~TcpActiveThread();
};

class KeepaliveListenThread {

	std::thread thread;

	Server& server;
	Endpoint& ep;

	std::mutex mtx;
	/* Used to wait in the keepalive listen loop */
	std::condition_variable cv;

	bool clientConnected = true;

	void keepaliveListenTask();

public:
	const socket_t clientSocket;
	explicit KeepaliveListenThread(Server& server, Endpoint& ep, socket_t clientSocket);
	~KeepaliveListenThread();

	bool isClientConnected() const { return clientConnected; }
	void disconnect() { xplatSockClose(clientSocket); }
};
