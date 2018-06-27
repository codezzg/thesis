#pragma once

#include "ext_mem_user.hpp"
#include "logging.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

#define ABORT(x)                 \
	do {                     \
		logging::err(x); \
		std::abort();    \
	} while (false)

/* A simple pool allocator type-safe class.
 * Internally uses a contiguous memory block organized as a linked list.
 * Alloc and dealloc operations are O(1).
 * Capacity is decided on creation and cannot be changed afterwards.
 * Requires explicit startup and shutdown.
 */
template <class T>
class PoolAllocator : public ExternalMemoryUser {

	void onInit() override
	{
		static_assert(sizeof(T) >= sizeof(uintptr_t), "sizeof(T) < sizeof(uintptr_t)!");
		pool = reinterpret_cast<uintptr_t*>(memory);
		capacity = memsize / sizeof(T);
		if (capacity == 0)
			ABORT("Created a PoolAllocator with 0 capacity");
		clear();
		logging::info("PoolAllocator initialized with ", memsize / 1024, " KiB of memory.");
	}

public:
	void clear() { _fillPool(); }

	T* alloc()
	{
		const uintptr_t firstFreeAddr = *pool;

		if (firstFreeAddr == 0)
			ABORT("PoolAllocator: Out of memory");

		const uintptr_t next = *reinterpret_cast<uintptr_t*>(firstFreeAddr);
		*pool = next;
		return reinterpret_cast<T*>(firstFreeAddr);
	}

	void dealloc(T* mem)
	{
		assert((uintptr_t)mem >= (uintptr_t)pool &&
			(uintptr_t)mem < (uintptr_t)((uint8_t*)pool + capacity * sizeof(T)) &&
			"Tried to deallocate memory not belonging to the pool!");

		const uintptr_t firstFreeAddr = *pool;
		*reinterpret_cast<uintptr_t*>(mem) = firstFreeAddr;
		*pool = reinterpret_cast<uintptr_t>(mem);
	}

	template <class... Args>
	T* create(Args&&... args)
	{
		T* mem = alloc();
		return new (mem) T(std::forward<Args>(args)...);
	}

	void destroy(T* obj)
	{
		obj->~T();
		dealloc(obj);
	}

#ifndef NDEBUG
	void dump() const
	{
		for (size_t i = 0; i < capacity; ++i) {
			printf("(%p): %p | ", pool + i, pool[i]);
		}
		printf("\n");
	}

	/** Calculates the remaining capacity of the pool "the hard way". Used for debugging */
	size_t realRemainingCapacity() const
	{
		size_t c = 0;
		uintptr_t nxt = *pool;
		while (nxt != 0) {
			++c;
			nxt = *reinterpret_cast<uintptr_t*>(nxt);
		}
		return c;
	}

	size_t totMem() const { return capacity * sizeof(T); }
#endif

private:
	uintptr_t* pool;
	std::size_t poolsize;
	std::size_t capacity;

	/** Fills the pool with a linked list of pointers */
	void _fillPool()
	{
		for (size_t i = 0; i < capacity - 1; ++i) {
			T* curAddr = reinterpret_cast<T*>(pool) + i;
			*reinterpret_cast<uintptr_t*>(curAddr) = reinterpret_cast<uintptr_t>(curAddr + 1);
		}
		*reinterpret_cast<uintptr_t*>(reinterpret_cast<T*>(pool) + capacity - 1) = 0x0;
	}
};

#undef ABORT
