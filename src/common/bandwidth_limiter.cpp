#include "bandwidth_limiter.hpp"
#include "endpoint.hpp"
#include "logging.hpp"
#include <cstring>
#include <exception>

using namespace logging;

void BandwidthLimiter::setSendLimit(float bytesPerSecond)
{
	if (bytesPerSecond < 0)
		throw std::invalid_argument("BandwidthLimiter::setSendLimit: bytesPerSecond must be >= 0!");

	std::lock_guard<std::mutex> lock{ mtx };
	tokenRate = bytesPerSecond;
}

// void BandwidthLimiter::setMaxQueueingDelay(std::chrono::duration<float> maxQueueingDelay)
//{
// if (tokenRate < 0)
// throw std::logic_error("setMaxQueueingDelay must be called after setSendLimit!");
// std::lock_guard<std::mutex> lock{ mtx };
//// bucketCapacity / tokenRate = maxQueueingDelay
// capacity = maxQueueingDelay.count() * tokenRate;
// info("maxQueueingDelay = ", maxQueueingDelay.count(), ", tokenRate = ", tokenRate);
// dataBuffer.reserve(capacity);
//}

void BandwidthLimiter::start()
{
	stop();

	operating = true;
	tokens = 0;

	refillThread = std::thread(std::bind(&BandwidthLimiter::refillTask, std::ref(*this)));
	info("BandwidthLimiter: started with maxTokens = ",
		maxTokens,
		", capacity = ",
		capacity,
		", tokenRate = ",
		tokenRate);
}

void BandwidthLimiter::stop()
{
	operating = false;

	cv.notify_all();

	if (refillThread.joinable()) {
		info("Joining refillThread...");
		refillThread.join();
	}

	// dataBuffer.clear();
}

/*
bool BandwidthLimiter::trySend(socket_t socket, const uint8_t* packet, std::size_t len)
{
	if (!operating)
		return true;

	std::lock_guard<std::mutex> lock{ mtx };

	if (static_cast<unsigned>(tokens) < len) {
		verbose("trySend returning false (have ", tokens, " / ", len, " tokens)");
		// Enqueue packet (don't ever overwrite)
		if (dataBuffer.size() == dataBuffer.capacity()) {
			warn("Losing a packet due to BandwidthLimiter. Bucket is full!");
		} else {
			EnqueuedPacket ep;
			ep.socket = socket;
			ep.len = len;
			memcpy(ep.data.data(), packet, len);
			dataBuffer.push(ep);
			info("Enqueued packet. Size = ", dataBuffer.size());
		}
		return false;
	}

	verbose("trySend returning true");
	tokens -= len;
	return true;
}
*/

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
			// tokens = sendEnqueuedPackets(tokens);
			verbose("tokens available: ", tokens);
		}
		cv.notify_all();

		// Sleep
		const auto delay = high_resolution_clock::now() - beginTime;
		std::this_thread::sleep_for(updateInterval - delay);
	}
	info("refillThread terminated.");
}

/*
int BandwidthLimiter::sendEnqueuedPackets(int tokensAvailable)
{
	assert(tokensAvailable >= 0);

	// No need to lock the mutex, as this method is only called by refillTask().

	int newTokensAvailable = tokensAvailable;

	while (operating && dataBuffer.size() > 0) {
		const auto packet = dataBuffer.peek();
		if (packet->len > static_cast<unsigned>(newTokensAvailable)) {
			// Can't send any more packets
			break;
		}
		// Send this packet
		sendPacket(packet->socket,
			packet->data.data(),
			packet->len,
			static_cast<unsigned>(EndpointFlags::BYPASS_BANDWIDTH_LIMITER));

		// Dequeue it
		newTokensAvailable -= packet->len;
		assert(newTokensAvailable >= 0);
		dataBuffer.pop();
	}

	return newTokensAvailable;
}*/
