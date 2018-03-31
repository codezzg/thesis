#pragma once

#include <chrono>

class Clock {
	uint64_t timeCycles;
	uint64_t latestTimeCycles;

	using clock = std::chrono::high_resolution_clock;

	static constexpr uint64_t secondsToCycles(float seconds) {
		return seconds * clock::period::den;
	}

	static constexpr float cyclesToSeconds(uint64_t cycles) {
		return static_cast<float>(cycles) / clock::period::den;
	}

public:
	static Clock& instance() {
		static Clock _instance;
		return _instance;
	}

	float timeScale = 1.f; // 0 => clock is paused
	bool paused = false;
	float targetDeltaTime = 1.f / 30.f;

	explicit Clock(float startTime = 0)
		: timeCycles(secondsToCycles(startTime))
		, latestTimeCycles(timeCycles)
	{}

	void update(float dt) {
		if (!paused) {
			latestTimeCycles = timeCycles;
			timeCycles += secondsToCycles(dt * timeScale);
		}
	}

	void step() {
		if (paused)
			timeCycles += secondsToCycles(targetDeltaTime * timeScale);
	}

	float deltaTime() const {
		return cyclesToSeconds(timeCycles - latestTimeCycles);
	}
};
