#pragma once

/** Platform independence layer for sockets */
#ifdef _WIN32
	#include <winsock2.h> // for INVALID_SOCKET
	#include <Ws2tcpip.h>
#else
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <unistd.h>
#endif

#ifdef _WIN32
using socket_t = SOCKET;
#else
using socket_t = int;
#endif

constexpr bool isValidSocket(socket_t sock) {
#ifdef _WIN32
	return sock != INVALID_SOCKET;
#else
	return sock >= 0;
#endif
}

constexpr socket_t invalidSocketID() {
#ifdef _WIN32
	return INVALID_SOCKET;
#else
	return -1;
#endif
}

bool xplatSocketInit();

bool xplatSocketCleanup();

int xplatSockClose(socket_t sock);
