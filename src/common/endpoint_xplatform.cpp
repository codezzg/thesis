#include "endpoint_xplatform.hpp"
#include <cstring>
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
	status = shutdown(sock, SD_BOTH);
	if (status == 0) status = closesocket(sock);
#else
	status = shutdown(sock, SHUT_RDWR);
	if (status == 0) status = close(sock);
#endif
	return status;
}

const char* xplatGetErrorString() {
	return std::strerror(xplatGetError());
}

int xplatGetError() {
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}
