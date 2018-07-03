#pragma once

#include "logging.hpp"
#include <chrono>

template <typename F>
void measure_ms(const char* name, LogLevel loglv, F&& f)
{
#ifdef NDEBUG
	f();
#else
	const auto beginTime = std::chrono::high_resolution_clock::now();
	f();
	const auto endTime = std::chrono::high_resolution_clock::now();
	logging::log(loglv,
		true,
		"[",
		name,
		"] time taken: ",
		std::chrono::duration_cast<std::chrono::microseconds>(endTime - beginTime).count() * 0.001,
		" ms");
#endif
}

#ifndef NDEBUG
#	define START_PROFILE(profname) const auto __prof_##profname##_begin = std::chrono::high_resolution_clock::now()
#	define END_PROFILE(profname, displayname, logLv)                                       \
		const auto __prof_##profname##_end = std::chrono::high_resolution_clock::now(); \
		logging::log(logLv,                                                             \
			true,                                                                   \
			"[",                                                                    \
			displayname,                                                            \
			"] time taken: ",                                                       \
			std::chrono::duration_cast<std::chrono::microseconds>(                  \
				__prof_##profname##_end - __prof_##profname##_begin)            \
					.count() *                                              \
				0.001,                                                          \
			" ms")
#else
#	define START_PROFILE(name)
#	define END_PROFILE(pname, dname, logLv)
#endif

