#pragma once

#include "ext_mem_user.hpp"
#include "logging.hpp"
#include <vector>

/** This class manages an externally provided memory buffer as a stack allocator.
 *  It does not perform any actual memory allocation or free, it merely gives back the
 *  proper pointers into the given buffer.
 */
class StackAllocator final : public ExternalMemoryUser {

	std::size_t used = 0;
	/** Sizes of the allocations made so far */
	std::vector<std::size_t> allocations;

	void onInit() override { logging::info("StackAllocator initialized with ", memsize / 1024, " KiB of memory."); }

public:
	template <typename T>
	T* alloc()
	{
		return static_cast<T*>(alloc(sizeof(T)));
	}

	void* alloc(std::size_t size)
	{
		if (used + size > memsize) {
			logging::err("StackAllocator: out of memory!");
			return nullptr;
		}

		auto ptr = memory + used;
		used += size;
		allocations.emplace_back(size);
		logging::debug("Allocating. # allocs so far: ",
			allocations.size(),
			" (used: ",
			used,
			" / ",
			memsize,
			" [",
			float(used) / memsize * 100,
			"%])");

		return ptr;
	}

	/** Allocates all the remaining memory. If `size` is not null, it's filled with the
	 *  size of the allocation.
	 */
	void* allocAll(std::size_t* size = nullptr)
	{
		if (size != nullptr)
			*size = memsize - used;

		return alloc(memsize - used);
	}

	void deallocLatest()
	{
		if (allocations.size() == 0) {
			logging::warn("StackAllocator: deallocLatest() called but no latest alloc exists.");
			return;
		}

		used -= allocations.back();
		allocations.pop_back();
		logging::debug("Deallocating. # allocs so far: ",
			allocations.size(),
			" (used: ",
			used,
			" / ",
			memsize,
			" [",
			float(used) / memsize * 100,
			"%])");
	}

	void deallocAll()
	{
		used = 0;
		logging::debug("Deallocating all the ", allocations.size(), " allocs.");
		allocations.clear();
	}

	std::size_t remaining() const { return memsize - used; }
};
