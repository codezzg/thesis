#pragma once

#include <chrono>
#include <iostream>
#include <string>

/** A simple FPS counter, designed to output to a stream. Use like this:
 *
 *  auto& myostream = std::cout;
 *  FPSCounter counter;
 *  counter.start();
 *  while (...) {
 *  	// do frame
 *  	counter.addFrame();
 *  	counter.report(myostream);
 *  }
 */
class FPSCounter final {
	using clock = std::chrono::high_resolution_clock;

	const std::string prelude;

	// Latest time the FPS were reported
	clock::time_point checkpoint;

	// Frames counted since checkpoint
	float frames = 0;

public:
	FPSCounter(const std::string& prelude = "FPS")
		: prelude{ prelude }
	{}

	float reportPeriod = 1;

	void start();
	void addFrame();
	void report(std::ostream& stream = std::cout);
};
