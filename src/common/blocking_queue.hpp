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
		mtx.unlock();
		cv.notify_one();
	}

	/** Retreives the first element of the queue.
	 *  If no elements are in the queue, blocks until one is added.
	 */
	T pop_or_wait()
	{
		if (CircularBuffer<T>::elements > 0) {
			std::lock_guard<std::mutex> lock{ mtx };
			return CircularBuffer<T>::pop_front();
		}

		std::unique_lock<std::mutex> ulk{ mtx };
		cv.wait(ulk, [this]() { return CircularBuffer<T>::elements > 0; });
		return CircularBuffer<T>::pop_front();
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

	std::size_t capacity() const { return CircularBuffer<T>::capacity(); }
	std::size_t size() const { return CircularBuffer<T>::size(); }
};
