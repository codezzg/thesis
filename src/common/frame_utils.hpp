#pragma once

#include <chrono>
#include <thread>

/** This class provides convenient RAII frame time limiting.
 *  Use like this:
 *
 *  while (cond) {
 *          // Must be defined at the beginning of the frame
 *          LimitFrameTime lft{ targetFrameTime };
 *          ...
 *  }
 *
 * WARNING: this class currently doesn't automatically account for time drifting, so if
 * the frame takes systematically longer than targetFrameTime to complete,
 * the delay will accumulate unless that's taken care of externally.
 * This can be done by doing:
 *
 * auto delay = 0ms;
 * while (cond) {
 *        LimitFrameTime lft{ targetFrameTime - delay };
 *        ...
 *        delay = lft.getFrameDelay();
 * }
 */
class LimitFrameTime final {
	const std::chrono::milliseconds targetFrameTime;
	const std::chrono::time_point<std::chrono::high_resolution_clock> beginFrameTime;

public:
	bool enabled = true;

	explicit LimitFrameTime(std::chrono::milliseconds targetFrameTime)
	        : targetFrameTime(targetFrameTime)
	        , beginFrameTime(std::chrono::high_resolution_clock::now()) {}

	~LimitFrameTime() {
		if (enabled) {
			const auto timeSpared = targetFrameTime - getFrameDuration();
			if (timeSpared.count() > 0)
				std::this_thread::sleep_for(timeSpared);
		}
	}

	std::chrono::milliseconds getFrameDuration() const {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
		        std::chrono::high_resolution_clock::now() - beginFrameTime);
	}

	std::chrono::milliseconds getFrameDelay() const {
		using namespace std::literals::chrono_literals;
		const auto delay = getFrameDuration() - targetFrameTime;
		return delay.count() > 0 ? delay : 0ms;
	}
};

template <typename T>
constexpr float asSeconds(const T& duration) {
	return std::chrono::duration_cast<std::chrono::duration<float>>(duration).count();
}
