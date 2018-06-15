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
