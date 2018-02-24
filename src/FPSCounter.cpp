#include "FPSCounter.hpp"

void FPSCounter::start() {
	checkpoint = clock::now();
}

void FPSCounter::addFrame() {
	++frames;
}

void FPSCounter::report(std::ostream& stream) {
	const auto now = clock::now();
	if (std::chrono::duration<float, std::chrono::seconds::period>(now - checkpoint).count() >= reportPeriod) {
		stream << "FPS: " << frames << std::endl;
		checkpoint = now;
		frames = 0;
	}
}
