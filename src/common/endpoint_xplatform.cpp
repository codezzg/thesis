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

std::string xplatGetCwd() {
	char buf[256];
#ifdef _WIN32
	int bytes = GetModuleFileName(nullptr, buf, 256);
	if (bytes == 0)
		return "[UNKNOWN]";

	const auto DIRSEP = '\\';

#else
	ssize_t bytes = 0;
	if (access("/proc/self/exe", F_OK) != -1) {
		// Linux
		bytes = readlink("/proc/self/exe", buf, 255);

	} else if (access("/proc/curproc/file", F_OK) != -1) {
		// BSD
		bytes = readlink("/proc/curproc/file", buf, 255);
	}

	if (bytes < 1) 
		return "[UNKNOWN]";

	buf[bytes] = '\0';

	const auto DIRSEP = '/';
#endif

	int len = strlen(buf);
	if (len < 1)
		return "[UNKNOWN]";

	// strip executable name
	for (int i = len - 1; i > -1; --i) {
		if (buf[i] == DIRSEP) {
			buf[i] = '\0';
			break;
		}
	}

	return std::string{ buf };
}