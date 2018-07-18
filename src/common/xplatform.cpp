#include "xplatform.hpp"

#ifdef _WIN32
#	define WINDOWS_LEAN_AND_MEAN 1
#	include <windows.h>
#	include <Shlwapi.h>
#else
#	include <csignal>
#	include <unistd.h>
#	include <libgen.h>
#endif

#include "logging.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <utility>

using namespace logging;

static signal_handler_t gHandler;
static bool gCalledExitHandler;

#ifdef _WIN32
static BOOL wrapper(DWORD signalType)
{
	if (gCalledExitHandler)
		return false;

	switch (signalType) {
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		gHandler();
		info("Exiting");
		gCalledExitHandler = true;
		std::exit(0);
	default:
		break;
	}
	return false;
}
#else
static void wrapper(int sig)
{
	if (gCalledExitHandler)
		return;

	info("Called exit handler via signal ", sig, ".");
	gCalledExitHandler = true;
	gHandler();
	info("Exiting");
	std::exit(0);
}
#endif

void xplatSetExitHandler(signal_handler_t handler)
{
	gHandler = handler;
}

bool xplatEnableExitHandler()
{
#ifdef _WIN32
	return SetConsoleCtrlHandler(wrapper, true);
#else
	std::signal(SIGINT, wrapper);
	std::signal(SIGTERM, wrapper);
	std::signal(SIGPIPE, wrapper);
	std::signal(SIGABRT, wrapper);
	return true;
#endif
}

std::string xplatGetCwd()
{
	constexpr auto len = 256;
	char buf[len];

#ifdef _WIN32
	int bytes = GetModuleFileName(nullptr, buf, len);
	if (bytes == 0) {
		return "[UNKNOWN]";
	}

#else
	ssize_t bytes = 0;
	if (access("/proc/self/exe", F_OK) != -1) {
		// Linux
		bytes = readlink("/proc/self/exe", buf, len - 1);

	} else if (access("/proc/curproc/file", F_OK) != -1) {
		// BSD
		bytes = readlink("/proc/curproc/file", buf, len - 1);
	}

	if (bytes < 1) {
		return "[UNKNOWN]";
	}

	buf[bytes] = '\0';

#endif
	// strip executable name
	for (int i = bytes - 1; i > -1; --i) {
		if (buf[i] == DIRSEP) {
			buf[i] = '\0';
			break;
		}
	}

	return std::string{ buf };
}

std::string xplatDirname(const char* path)
{
	// Copy path into a modifiable buffer
	std::string res;
	const auto len = strlen(path);
	char* buf = new char[len + 1];
	strncpy(buf, path, len + 1);
#ifdef _WIN32
	PathRemoveFileSpec(buf);
	res = std::string{ buf };
#else
	res = std::string{ dirname(buf) };
#endif
	delete[] buf;
	return res;
}

std::string xplatBasename(const char* path)
{
	// Copy path into a modifiable buffer
	std::string res;
	const auto len = strlen(path);
	char* buf = new char[len + 1];
	strncpy(buf, path, len + 1);
#ifdef _WIN32
	// TODO
	res = path;
#else
	res = std::string{ basename(buf) };
#endif
	delete[] buf;
	return res;
}

std::string xplatPath(std::string&& path)
{
#ifdef _WIN32
	std::replace(path.begin(), path.end(), '/', '\\');
#else
	std::replace(path.begin(), path.end(), '\\', '/');
#endif
	return path;
}

#ifdef _WIN32
// Helper function for setting thread name.
// See: https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code

#	pragma pack(push, 8)
struct THREADNAME_INFO {
	DWORD dwType = 0x1000;   // Must be 0x1000.
	LPCSTR szName;           // Pointer to name (in user addr space).
	DWORD dwThreadID;        // Thread ID (-1=caller thread).
	DWORD dwFlags = 0;       // Reserved for future use, must be zero.
};
#	pragma pack(pop)

static void _winSetThreadName(DWORD dwThreadID, const char* threadName)
{
	constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

	THREADNAME_INFO info;
	info.szName = threadName;
	info.dwThreadID = dwThreadID;
#	pragma warning(push)
#	pragma warning(disable : 6320 6322)
	__try {
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
#	pragma warning(pop)
}
#endif

void xplatSetThreadName(std::thread& thread, const char* name)
{
#ifdef _WIN32
	_winSetThreadName(thread.native_handle(), name);
#else
	// XXX: this is probably very vulnerable
	char* nameRW = const_cast<char*>(name);
	if (strlen(name) > 16) {
		strncpy(nameRW, name, 15);
		nameRW[15] = '\0';
	}
	pthread_setname_np(thread.native_handle(), nameRW);
#endif
}
