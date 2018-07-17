#pragma once

#include "circular_buffer.hpp"
#include <condition_variable>
#include <mutex>

template <typename T>
class BlockingQueue : private CircularBuffer<T> {

	std::mutex mtx;
	std::condition_variable cv;

public:
	void push(const T& elem)
	{
		mtx.lock();
		CircularBuffer<T>::push_back(elem);
		logging::info("elements are now ", CircularBuffer<T>::elements);
		mtx.unlock();
		cv.notify_one();
	}

	/** Retreives the first element of the queue.
	 *  If no elements are in the queue, blocks until one is added.
	 */
	T pop_or_wait()
	{
		logging::info("pop: elements are now ", CircularBuffer<T>::elements);
		if (CircularBuffer<T>::elements > 0) {
			std::lock_guard<std::mutex> lock{ mtx };
			auto res = CircularBuffer<T>::pop_front();
			logging::info("BlockingQueue: returning immediately ", res);
			return res;
			// return CircularBuffer<T>::pop_front();
		}

		std::unique_lock<std::mutex> ulk{ mtx };
		cv.wait(ulk, [this]() { return CircularBuffer<T>::elements > 0; });
		logging::info("pop after wait: elements are now ",
			CircularBuffer<T>::elements,
			" and mem[0] is ",
			CircularBuffer<T>::memStart[0]);
		// return CircularBuffer<T>::pop_front();
		auto res = CircularBuffer<T>::pop_front();
		logging::info("BlockingQueue: returning after waiting ", res);
		return res;
	}

	/** Non-blocking version of `pop_or_wait`.
	 *  @return true if there was at least one element, false otherwise.
	 */
	bool try_pop(T& value)
	{
		if (CircularBuffer<T>::elements == 0)
			return false;
		std::lock_guard<std::mutex> lock{ mtx };
		value = CircularBuffer<T>::pop_front();
		return true;
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock{ mtx };
		CircularBuffer<T>::clear();
	}

	void reserve(std::size_t n)
	{
		std::lock_guard<std::mutex> lock{ mtx };
		CircularBuffer<T>::reserve(n);
	}
};
