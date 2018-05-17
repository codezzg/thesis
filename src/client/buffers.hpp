#pragma once

/*
 * Utilities to deal with Vulkan buffers.
 * A Buffer has a Vulkan Handle, an underlying memory, size and offset
 * and may have a pointer mapped to host memory.
 * When using buffers, prefer creating, mapping, unmapping and destroying them in
 * group, not singularly, as this minimizes the overhead of allocating and freeing memory.
 * To do that, see VulkanAllocator, create/destroy/map/unmapBuffers* functions.
 */

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <tuple>

struct Application;

struct Buffer final {
	VkBuffer handle;
	VkDeviceMemory memory;
	VkDeviceSize size;
	/** Offset in the underlying memory */
	VkDeviceSize offset;
	/** Host-mapped pointer, if any */
	void *ptr = nullptr;
};

struct MVPUniformBufferObject final {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

struct CompositionUniformBufferObject final {
	glm::vec4 viewPos; // w used as 'showGbufTex'
};

/** Use this class to allocate a bunch of buffers at once.
 *  This allocator will attempt to minimize the number of allocations by reusing the same memory
 *  for multiple buffers with proper offsets.
 */
class BufferAllocator final {
	std::vector<VkBufferCreateInfo> createInfos;
	std::vector<VkMemoryPropertyFlags> properties;
	std::vector<Buffer*> buffers;

public:
	using BufferCreateInfo = std::tuple<VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags>;

	/** Schedules a new buffer to be created and binds it to `buffer`. */
	void addBuffer(Buffer& buffer,
			VkDeviceSize size,
			VkBufferUsageFlags flags,
			VkMemoryPropertyFlags properties);

	void addBuffer(Buffer& buffer, const BufferCreateInfo& info);

	/** Creates the scheduled buffers and allocates their memory. */
	void create(const Application& app);
};

/** Creates a single buffer with its own allocation.
 *  NOTE: this is a pessimizing memory access pattern, try to avoid it!
 */
Buffer createBuffer(
		const Application& app,
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties);

/** Creates a buffer suited for use as a staging buffer and maps its memory to the host.
 *  Note: in the case of staging buffers it's probably better to create a single one of them
 *  and reuse it until necessary rather than creating it alongside others with BufferAllocator.
 *  This way, we can free the staging buffer as soon as it's not used anymore and not waste
 *  memory.
 */
Buffer createStagingBuffer(const Application& app, VkDeviceSize size);

void copyBuffer(const Application& app, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
void copyBufferToImage(const Application& app, VkBuffer buffer, VkImage image, 
		uint32_t width, uint32_t height, VkDeviceSize bufOffset = 0);

void destroyBuffer(VkDevice device, Buffer& buffer);

/** Destroys all given buffers and frees the underlying memory in a safe way
 *  (i.e. frees once if several buffers share the same memory)
 */
void destroyAllBuffers(VkDevice device, const std::vector<Buffer>& buffers);

/** Given the `buffers`, maps them to host memory in a proper way (i.e. maps each memory only once).
 *  - buffers must have already been created and bound to memory 
 *  - buffers must have the HOST_COHERENT bit set 
 */
void mapBuffersMemory(VkDevice device, 
		/* inout */ const std::vector<Buffer*>& buffers);

/** Does the opposite of `mapBuffersMemory` */
void unmapBuffersMemory(VkDevice device, const std::vector<Buffer>& buffers);

/** @return the parameters to create a screen quad buffer with. */
BufferAllocator::BufferCreateInfo getScreenQuadBufferProperties();

/** Fills `screenQuadBuf` with vertex data using `stagingBuf` as a staging buffer.
 *  Both of the buffers must already be valid.
 */
void fillScreenQuadBuffer(const Application& app, Buffer& screenQuadBuf, Buffer& stagingBuf);
