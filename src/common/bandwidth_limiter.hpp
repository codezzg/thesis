#pragma once

#include "circular_buffer.hpp"
#include "config.hpp"
#include "endpoint_xplatform.hpp"
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

/** A bandwidth limiter using the token bucket algorithm.
 *  Runs in its own thread.
 */
class BandwidthLimiter {
	/** Guards access to the class parameters */
	mutable std::mutex mtx;

	/** Our operating thread */
	std::thread refillThread;

	void refillTask();

	/** Whether we're limiting the bandwidth or not */
	bool operating = false;

	/** Interval at which refillThread updates (seconds) */
	const std::chrono::duration<float> updateInterval{ 0.2 };

	/** Token refill rate (in Hz). This is the simulated bandwidth. */
	float tokenRate = -1;

	/** Bucket capacity (max number of packets that can accumulate in the bucket) */
	std::size_t capacity = 1000;

	/** Burst size (max tokens that can accumulate in the bucket) */
	int maxTokens = 10000 * cfg::PACKET_SIZE_BYTES + 1;

	/** Tokens currently in the bucket. Each token represents a byte. */
	int tokens = 0;

public:
	std::mutex cvMtx;
	std::condition_variable cv;

	/** Sets the max amount of bytes sent per second by all sockets to `limit` (cumulative).
	 *  Only applies to sends called through endpoint functions, such as `sendPacket`.
	 */
	void setSendLimit(float bytesPerSecond);

	// void setMaxQueueingDelay(std::chrono::duration<float> maxQueueingDelay);

	/** Sets the max capacity of this limiter. */
	void setCapacity(std::size_t maxPackets);

	/** Starts the limiter in a new thread. If it was already active, it stops the previous thread. */
	void start();

	/** Stops the limiter if active, and joins its thread. */
	void stop();

	/** Requests `n` tokens from the bucket. If at least `n` tokens are available,
	 *  detract them from the bucket and return `true`. Else, do nothing and return `false`.
	 *  If this limiter is not operating, always return `true`.
	 */
	bool requestTokens(int n);

	int getTokens() const { 
		std::lock_guard<std::mutex> lock{ mtx };
		return tokens;
	}

	bool isActive() const { return operating; }
};
