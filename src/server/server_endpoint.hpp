#pragma once

#include "endpoint.hpp"
#include "ext_mem_user.hpp"
#include "frame_data.hpp"
#include "hashing.hpp"
#include "shared_resources.hpp"
#include "vertex.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>

struct Server;
struct ServerSharedData;

/** This class implements the active server thread which sends messages to client via an UDP socket. */
class UdpActiveThread : public ExternalMemoryUser {

	std::thread thread;

	Server& server;
	Endpoint& ep;

	void udpActiveTask();

public:
	/** Constructs a ServerActiveEndpoint owned by `server`. */
	explicit UdpActiveThread(Server& server, Endpoint& ep);
	~UdpActiveThread();
};

/** This class implements the passive server thread which receives client ACKs. */
class UdpPassiveThread {

	std::thread thread;

	Server& server;
	Endpoint& ep;

	void udpPassiveTask();

public:
	explicit UdpPassiveThread(Server& server, Endpoint& ep);
	~UdpPassiveThread();
};

