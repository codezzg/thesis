#pragma once

#include <chrono>
#include <iostream>

class FPSCounter final {
	using clock = std::chrono::high_resolution_clock;

	// Latest time the FPS were reported
	clock::time_point checkpoint;

	// Frames counted since checkpoint
	uint32_t frames = 0;

public:
	float reportPeriod = 1;

	void start();
	void addFrame();
	void report(std::ostream& stream = std::cout);
};
