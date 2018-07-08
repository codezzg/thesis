#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>

template <typename T>
class CircularBuffer {
	T* memStart = nullptr;
	std::size_t cap = 0;

	/** Offset from `memStart` (in units of sizeof(T)) where the next element
	 *  will be inserted.
	 */
	unsigned curIdx = 0;
	/** How many elements are currently in the buffer */
	std::size_t elements = 0;

public:
	explicit CircularBuffer() {}

	explicit CircularBuffer(std::size_t capacity) { reserve(capacity); }

	void reserve(std::size_t newCap)
	{
		if (newCap > cap) {
			memStart = reinterpret_cast<T*>(realloc(memStart, newCap / sizeof(T)));
			curIdx = 0;
		}
		cap = newCap;
	}

	~CircularBuffer() { free(memStart); }

	void push(const T& elem)
	{
		memStart[curIdx] = elem;
		curIdx = (curIdx + 1) % cap;
		elements = std::min(elements + 1, cap);
	}

	T pop()
	{
		assert(elements > 0);
		--elements;
		curIdx = (curIdx + cap - 1) % cap;
		return memStart[curIdx];
	}

	T* peek() const { return &memStart[curIdx - 1]; }

	void clear() { elements = 0; }

	std::size_t size() const { return elements; }
	std::size_t capacity() const { return cap; }
};
