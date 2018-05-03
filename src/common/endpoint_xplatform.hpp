#pragma once

#include <string>
/** Platform independence layer for sockets */
#ifdef _WIN32
	#include <WinSock2.h>
	#include <Ws2tcpip.h>
#else
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <unistd.h>
#endif

#ifdef _WIN32
using socket_t = SOCKET;
using socket_connect_op = int (__stdcall*) (socket_t, const sockaddr*, int);
#else
using socket_t = int;
using socket_connect_op = int (*) (socket_t, const sockaddr*, socklen_t);
#endif

/** Checks if the given handle represents a valid socket */
constexpr bool xplatIsValidSocket(socket_t sock) {
#ifdef _WIN32
	return sock != INVALID_SOCKET;
#else
	return sock >= 0;
#endif
}

/** Returns an invalid socket ID */
constexpr socket_t xplatInvalidSocketID() {
#ifdef _WIN32
	return INVALID_SOCKET;
#else
	return -1;
#endif
}

/** Used to setup the socket system. Only use once, before any socket is created. */
bool xplatSocketInit();

/** Used to teardown the socket system. Only use once, on program end. */
bool xplatSocketCleanup();

/** Closes a socket */
int xplatSockClose(socket_t sock);

/** Returns the latest error string */
const char* xplatGetErrorString();

/** Returns the latest error code */
int xplatGetError();

std::string xplatGetCwd();
