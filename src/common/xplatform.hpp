#pragma once

#include <functional>
#include <string>

namespace std {
class thread;
}

#ifdef _WIN32

constexpr char DIRSEP = '\\';

#else

constexpr char DIRSEP = '/';

#endif

using signal_handler_t = std::function<void()>;

/** @return the absolute path to the executable's directory.  */
std::string xplatGetCwd();

/** Call this to enable the use of a custom exit handler.
 *  @see xplatSetExitHandler
 */
bool xplatEnableExitHandler();

/** Sets `handler` as the custom exit handler.
 *  It will only be called when the program exits abnormally (i.e. is terminated by a signal).
 *  If you wish to call it also on normal termination, use std::atexit.
 */
void xplatSetExitHandler(signal_handler_t handler);

std::string xplatDirname(const char* path);
std::string xplatBasename(const char* path);

std::string xplatPath(std::string&& str);

void xplatSetThreadName(std::thread& thread, const char* name);
