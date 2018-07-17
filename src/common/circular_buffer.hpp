#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#ifndef NDEBUG
#	include "logging.hpp"
#endif

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

	void reserve(std::size_t newCap)
	{
		if (newCap > cap) {
			memStart = reinterpret_cast<T*>(realloc(memStart, newCap * sizeof(T)));
		}
		cap = newCap;
	}

	~CircularBuffer() { free(memStart); }

	void push_back(const T& elem)
	{
#ifndef NDEBUG
		if (elements == cap) {
			logging::warn("Warning: overriding element of CircularBuffer! (cap = ", cap, ")");
		}
#endif
		memStart[endIdx] = elem;
		endIdx = (endIdx + 1) % cap;
		if (endIdx == startIdx) {
			// We reached startIdx after looping the buffer: advance it.
			startIdx = (startIdx + 1) % cap;
		}
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
	};

	iter iter_start() const { return iter{ startIdx }; }
	bool iter_next(iter& it, T& value) const
	{
		if (it.idx != endIdx) {
			value = memStart[it.idx];
			it.idx = (it.idx + 1) % cap;
			return true;
		}
		return false;
	}
};
