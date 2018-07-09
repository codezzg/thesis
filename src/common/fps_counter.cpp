#include "fps_counter.hpp"
#include <cassert>

void FPSCounter::start()
{
	checkpoint = clock::now();
}

void FPSCounter::addFrame()
{
	++frames;
}

void FPSCounter::report(std::ostream& stream)
{
	const auto now = clock::now();
	if (std::chrono::duration<float, std::chrono::seconds::period>(now - checkpoint).count() >= reportPeriod) {
		assert(reportPeriod > 0);
		frames /= reportPeriod;
		stream << prelude << ": " << frames << " fps (" << 1000.0 / frames << " ms)\n";
		checkpoint = now;
		frames = 0;
	}
}
