#pragma once

#include <string>

using signal_handler_t = void(*)();

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

std::string xplatDirname(const char *path);
