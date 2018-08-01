#include "bandwidth_limiter.hpp"
#include "endpoint.hpp"
#include "logging.hpp"
#include "xplatform.hpp"
#include <cstring>
#include <exception>
#include <functional>

using namespace logging;

void BandwidthLimiter::setSendLimit(float bytesPerSecond)
{
	if (bytesPerSecond < 0)
		throw std::invalid_argument("BandwidthLimiter::setSendLimit: bytesPerSecond must be >= 0!");

	std::lock_guard<std::mutex> lock{ mtx };
	tokenRate = bytesPerSecond;
}

void BandwidthLimiter::start()
{
	stop();

	operating = true;
	tokens = 0;

	refillThread = std::thread(std::bind(&BandwidthLimiter::refillTask, std::ref(*this)));
	xplatSetThreadName(refillThread, "BandwidthLimiter");
	info("BandwidthLimiter: started with maxTokens = ", maxTokens, ", tokenRate = ", tokenRate);
}

void BandwidthLimiter::stop()
{
	operating = false;

	cv.notify_all();

	if (refillThread.joinable()) {
		info("Joining refillThread...");
		refillThread.join();
	}
}

bool BandwidthLimiter::requestTokens(int n)
{
	if (!operating)
		return true;

	std::lock_guard<std::mutex> lock{ mtx };

	if (n < tokens) {
		tokens -= n;
		return true;
	}

	return false;
}

void BandwidthLimiter::refillTask()
{
	using namespace std::chrono;

	while (operating) {
		auto beginTime = high_resolution_clock::now();
		{
			std::lock_guard<std::mutex> lock{ mtx };

			auto nTokensToRefill = static_cast<int>(tokenRate * updateInterval.count());
			tokens = std::min(maxTokens, tokens + nTokensToRefill);
			verbose("tokens available: ", tokens);
		}
		cv.notify_all();

		// Sleep
		const auto delay = high_resolution_clock::now() - beginTime;
		std::this_thread::sleep_for(updateInterval - delay);
	}
	info("refillThread terminated.");
}

