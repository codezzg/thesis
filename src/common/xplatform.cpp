#include "xplatform.hpp"

#ifdef _WIN32
	#define WINDOWS_LEAN_AND_MEAN 1
	#include <windows.h>
#endif
#include <utility>
#include <iostream>

#ifdef _WIN32
static signal_handler_t gHandler;

static BOOL wrapper(DWORD signalType) {
	switch (signalType) {
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		gHandler();
	default:
		break;
	}
	return false;
}
#endif

void xplatSetExitHandler(signal_handler_t handler) {
	std::atexit(handler);
	gHandler = handler;
}

bool xplatEnableExitHandler() {
#ifdef _WIN32
	return SetConsoleCtrlHandler(wrapper, true);
#else
	// TODO
#endif
}

const char *_cwd = nullptr;

const char* xplatGetCwd() {
	if (_cwd != nullptr)
		return _cwd;

	char buf[256];
#ifdef _WIN32
	int bytes = GetModuleFileName(nullptr, buf, 256);
	if (bytes == 0)
		return "[UNKNOWN]";

	const char DIRSEP = '\\';

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

	const char DIRSEP = '/';
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

	// Note: windows has no strndup
	_cwd = reinterpret_cast<const char*>(malloc(len * sizeof(char)));
	strncpy(const_cast<char*>(_cwd), buf, len);

	return _cwd;
}
