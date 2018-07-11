#pragma once

#include "endpoint.hpp"
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

	/** The TCP main loop */
	bool msgLoop(socket_t clientSocket, sockaddr_in clientAddr);

	/** Sends all the one-time data the client needs. */
	bool sendOneTimeData(socket_t clientSocket);

	void dropClient(socket_t clientSocket);

	void tcpActiveTask();

public:
	explicit TcpActiveThread(Server& server, Endpoint& ep);
	~TcpActiveThread();
};

// class KeepaliveListenThread {

// std::thread thread;

// const socket_t clientSocket;

// std::mutex mtx;
//[>* Used to wait in the keepalive listen loop <]
// std::condition_variable cv;

// void keepaliveListenTask();

// public:
// explicit KeepaliveListenThread(socket_t clientSocket);
//~KeepaliveListenThread();
//};
