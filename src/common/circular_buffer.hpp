#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#ifndef NDEBUG
#	include "logging.hpp"
#endif

/** This class implements a FIFO container that supports O(1) insertion and removal,
 *  as well as efficient and cache-friendly iteration on its elements.
 *  The max number of elements in this container is fixed and determined by `reserve`,
 *  but it can be grown while the buffer is being used by calling `reserve` again.
 *  This container never automatically grows, so it's up to the user to keep track of
 *  its filling.
 *  If a new element is added while the buffer is full, the oldest element is overwritten.
 *  It is a fatal error to pop while there are no elements in this collection.
 */
template <typename T>
class CircularBuffer {
protected:
	T* memStart = nullptr;
	std::size_t cap = 0;

	/** Offset from `memStart` (in units of sizeof(T)) where the head of the buffer is. */
	unsigned startIdx = 0;

	/** Offset from `memStart` (in units of sizeof(T)) where the tail of the buffer is
	 *  (i.e. where the next element will be placed).
	 */
	unsigned endIdx = 0;

	/** How many elements are currently in the buffer. */
	std::size_t elements = 0;

public:
	explicit CircularBuffer() {}

	explicit CircularBuffer(std::size_t capacity) { reserve(capacity); }

	/** Changes the capacity of this CircularBuffer to `newCap.`
	 *  This means that exactly `newCap` elements will be contained at once and if more elements are
	 *  added, the oldest ones are overwritten.
	 *  This function reallocates memory if `newCap` > `cap`.
	 */
	void reserve(std::size_t newCap)
	{
		if (newCap > cap) {
			memStart = reinterpret_cast<T*>(realloc(memStart, newCap * sizeof(T)));
		}
		cap = newCap;
		elements = std::min(elements, cap);
		endIdx = elements % cap;
	}

	~CircularBuffer() { free(memStart); }

	void push_back(const T& elem)
	{
		if (elements > 0 && endIdx == startIdx) {
			// We reached startIdx after looping the buffer: advance it.
			startIdx = (startIdx + 1) % cap;
#ifndef NDEBUG
			logging::warn("Warning: overriding element of CircularBuffer! (cap = ", cap, ")");
#endif
		}

		memStart[endIdx] = elem;
		endIdx = (endIdx + 1) % cap;
		elements = std::min(elements + 1, cap);
		assert(startIdx < cap && endIdx < cap && elements <= cap);
	}

	T pop_front()
	{
		assert(elements > 0);
		--elements;
		T res = memStart[startIdx];
		startIdx = (startIdx + 1) % cap;
		assert((startIdx == endIdx) == (elements == 0 || elements == cap));
		assert(startIdx < cap && endIdx < cap && elements <= cap);
		return res;
	}

	void clear()
	{
		elements = 0;
		endIdx = startIdx;
		assert(startIdx < cap && endIdx < cap && elements <= cap);
	}

	std::size_t size() const { return elements; }
	std::size_t capacity() const { return cap; }

	struct iter {
		unsigned idx;
		std::size_t nElem;
	};

	/** Iterates the CircularBuffer.
	 *  To avoid unexpected behaviour, don't change the buffer's capacity while iterating.
	 */
	iter iter_start() const { return iter{ startIdx, 0 }; }
	bool iter_next(iter& it, T& value) const
	{
		if (it.nElem < elements) {
			value = memStart[it.idx];
			it.idx = (it.idx + 1) % cap;
			it.nElem++;
			return true;
		}
		return false;
	}
};
