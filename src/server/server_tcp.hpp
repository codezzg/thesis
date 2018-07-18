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
	bool msgLoop(socket_t clientSocket);

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

/** Utility mixin class */
class ServerSlaveThread {
protected:
	std::thread thread;

	Server& server;
	const Endpoint& ep;
	const socket_t clientSocket;

	explicit ServerSlaveThread(Server& server, const Endpoint& ep, socket_t clientSocket)
		: server{ server }
		, ep{ ep }
		, clientSocket{ clientSocket }
	{}

public:
	bool clientConnected = true;
};

class KeepaliveListenThread : public ServerSlaveThread {

	std::mutex mtx;
	/* Used to wait in the keepalive listen loop */
	std::condition_variable cv;

	void keepaliveListenTask();

public:
	explicit KeepaliveListenThread(Server& server, const Endpoint& ep, socket_t clientSocket);
	~KeepaliveListenThread();
};

/** This thread listens on the TCP endpoint and routes the incoming messages
 *  either to the keepalive or to the general TCP message queue.
 */
class TcpReceiveThread : public ServerSlaveThread {

	void receiveTask();

public:
	explicit TcpReceiveThread(Server& server, const Endpoint& ep, socket_t clientSocket);
	~TcpReceiveThread();
};
