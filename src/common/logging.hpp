#pragma once

#include <iostream>

enum LogLevel {
	LOGLV_NONE = 0,
	LOGLV_ERR = 1,
	LOGLV_WARN = 2,
	LOGLV_INFO = 3,
	LOGLV_DEBUG = 4
};

extern LogLevel gDebugLv;

inline void log(LogLevel debugLv, bool breakLine) {
	if (gDebugLv >= debugLv && breakLine)
		std::cerr << "\n";
}

template <typename Arg, typename... Args>
inline void log(LogLevel debugLv, bool breakLine, Arg&& arg, Args&&... args) {
	if (gDebugLv < debugLv) return;
	std::cerr << arg << " ";
	log(debugLv, breakLine, std::forward<Args>(args)...);
}

template <typename... Args>
inline void err(Args&&... args) {
	log(LOGLV_ERR, true, "[E]", std::forward<Args>(args)...);
}

template <typename... Args>
inline void warn(Args&&... args) {
	log(LOGLV_WARN, true, "[W]", std::forward<Args>(args)...);
}

template <typename... Args>
inline void info(Args&&... args) {
	log(LOGLV_INFO, true, "[I]", std::forward<Args>(args)...);
}

template <typename... Args>
inline void debug(Args&&... args) {
	log(LOGLV_DEBUG, true, "[D]", std::forward<Args>(args)...);
}
