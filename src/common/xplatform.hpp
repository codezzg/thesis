#pragma once

#include <functional>
#include <string>

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

/** Sets `handler` as the custom exit handler. It will be called when the
 *  program exits normally or is terminated by a signal.
 */
void xplatSetExitHandler(signal_handler_t handler);

std::string xplatDirname(const char* path);
std::string xplatBasename(const char* path);

std::string xplatPath(std::string&& str);
