#pragma once

#include "buffers.hpp"
#include "hashing.hpp"
#include "utils.hpp"
#include <ostream>
#include <unordered_map>
#include <vector>

struct Application;

/** A SubBuffer is part of a parent Buffer. */
struct SubBuffer : public Buffer {
	/** Offset in the underlying Buffer */
	VkDeviceSize bufOffset;
};

/** A struct containing multiple logical buffers inside as little Vulkan Buffers as possible.
 *  Can grow when needed and deallocate single "buffers" inside it.
 *  Allows map-like access.
 *  NOTES:
 *  - all the Buffers returned by this class must NOT be manually destroyed: they are all
 *    owned by the BufferArray, which will destroy them by calling `cleanup()` on the BufferArray.
 *  - all the logical buffers instanced from this Array share the same usage and memory flags.
 *  - `addBuffer` is designed to be usable when the final number of buffers can't be known a priori,
 *    so it may need to make separate memory allocations for separate buffers, unless enough memory
 *    was reserved with reserve().
 */
class BufferArray {

	const Application* app = nullptr;
	VkBufferUsageFlags usage;
	VkMemoryPropertyFlags properties;
	VkDeviceSize minAlign;
	/** Minimum size of the allocated buffers */
	VkDeviceSize minBufferSize;

	bool mappingBuffers = false;

	struct BufferFreeRange {
		VkDeviceSize start;
		VkDeviceSize end;
		constexpr int64_t len() const { return end - start; }
	};

	friend std::ostream& operator<<(std::ostream&, BufferFreeRange);

	/** Each of this buffer is an actual Vulkan buffer allocated separately.
	 *  Once allocated, it remains so until the entire array is deallocated,
	 *  so it can be reused later.
	 */
	std::vector<Buffer> backingBuffers;
	/** For each backingBuffer, the list of free ranges */
	std::vector<std::vector<BufferFreeRange>> bufferFreeRanges;
	std::unordered_map<StringId, SubBuffer> allocatedBuffers;

public:
	explicit BufferArray(VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
		: usage{ usage }
		, properties{ properties }
	{}
	explicit BufferArray(BufferArray&& other) = default;

	BufferArray& operator=(BufferArray&& other) = default;

	/** Needs to be called before calling reserve() or addBuffer().
	 *  If `minBufferSize` == 0, it will be set to a small multiple of minAlign.
	 */
	void initialize(const Application& app, VkDeviceSize minBufferSize = 0);

	/** Allocates a backing buffer which is at least `initialSize` bytes. */
	void reserve(VkDeviceSize initialSize);

	/** Maps all currently and future allocated buffers to host memory.
	 *  Pointers obtained via `addBuffer` or `getBuffer` will have their `ptr` updated.
	 *  Only valid if `properties` includes HOST_VISIBLE.
	 */
	void mapAllBuffers();
	/** Unmaps all currently allocated buffers and stops mapping future ones. */
	void unmapAllBuffers();

	void cleanup();

	/** Adds a logical buffer to the array and returns it.
	 *  If the buffer fits the already-allocated buffer(s), it will be part of one of them,
	 *  else, a new backing Buffer will be allocated.
	 */
	SubBuffer* addBuffer(StringId name, VkDeviceSize size);

	/** @return The SubBuffer named `name` or nullptr if it doesn't exist. */
	SubBuffer* getBuffer(StringId name) const;

	/** Invalidates SubBuffer `name` and marks its memory as available. */
	void rmBuffer(StringId name);

	void dump() const
	{
		for (unsigned i = 0; i < backingBuffers.size(); ++i)
			std::cout << "freeRanges[" << i << "] = " << listToString(bufferFreeRanges[i]) << "\n";
	}
};

inline std::ostream& operator<<(std::ostream& s, BufferArray::BufferFreeRange r)
{
	s << "{ start: " << r.start << ", end: " << r.end << " (len = " << r.len() << ") }";
	return s;
}
