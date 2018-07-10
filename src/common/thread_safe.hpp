#pragma once

#include "circular_buffer.hpp"
#include <cstddef>
#include <mutex>

template <typename T>
class ConcurrentQueue : public CircularBuffer<T> {

	using iter = typename CircularBuffer<T>::iter;

	mutable std::mutex mtx;

public:
	void push_back(const T& elem)
	{
		std::lock_guard<std::mutex> lock{ mtx };
		CircularBuffer<T>::push_back(elem);
	}

	/** Pushes all elements of `elems` in the queue.
	 *  `elems` must not be concurrently accessed.
	 */
	template <typename E>
	void push_all_back(const E& elems)
	{
		std::lock_guard<std::mutex> lock{ mtx };
		for (const auto& e : elems) {
			CircularBuffer<T>::push_back(e);
		}
	}

	T pop_front()
	{
		std::lock_guard<std::mutex> lock{ mtx };
		return CircularBuffer<T>::pop_front();
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

	/** Locks the queue. Must be called before `iter_start` in case of iteration. */
	void lock() const { mtx.lock(); }

	/** Unlocks the queue. */
	void unlock() const { mtx.unlock(); }
};
