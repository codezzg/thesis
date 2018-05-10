#include "endpoint_xplatform.hpp"
#include <cstring>
#include <iostream>
#include "logging.hpp"
#ifndef _WIN32
	#include <cerrno>
#endif

bool xplatSocketInit() {
#ifdef _WIN32
	WSADATA wsaData;
	return WSAStartup(MAKEWORD(1, 1), &wsaData) == 0;
#else
	return true;
#endif
}

bool xplatSocketCleanup() {
#ifdef _WIN32
	return WSACleanup() == 0;
#else
	return true;
#endif
}

int xplatSockClose(socket_t sock) {
	int status = 0;
#ifdef _WIN32
	// This may fail if the socket is UDP, just don't care
	status = shutdown(sock, SD_BOTH);
	if (status == 0 || WSAGetLastError() == WSAENOTCONN) {
		status = closesocket(sock);
#else
	// This may fail if the socket is UDP, just don't care
	status = shutdown(sock, SHUT_RDWR);
	if (status == 0 || errno == ENOTCONN) {
		status = close(sock);
#endif
	} else warn("Error shutting down the socket: ", xplatGetErrorString(), " (", xplatGetError(), ")");

	return status;
}

std::string xplatGetErrorString() {
	char buf[256];
	strerror_s(buf, 256, xplatGetError());
	return std::string{ buf };
}

int xplatGetError() {
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}