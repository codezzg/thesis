#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

struct Application;

struct Buffer final {
	VkBuffer handle;
	VkDeviceMemory memory;
	VkDeviceSize size;
	/** Offset in the underlying memory */
	VkDeviceSize offset;

	void destroy(VkDevice device) {
		vkDestroyBuffer(device, handle, nullptr);
		vkFreeMemory(device, memory, nullptr);
	}
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
	/** Schedules a new buffer to be created and binds it to `buffer`. */
	void addBuffer(Buffer& buffer,
			VkDeviceSize size,
			VkBufferUsageFlags flags,
			VkMemoryPropertyFlags properties);

	/** Creates the scheduled buffers and allocates their memory. */
	void create(const Application& app);
};

/** Creates a single buffer with its own allocation */
Buffer createBuffer(
		const Application& app,
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties);

void copyBuffer(const Application& app, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
void copyBufferToImage(const Application& app, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
Buffer createScreenQuadVertexBuffer(const Application& app);

/** Destroys all given buffers and frees the underlying memory in a safe way
 *  (i.e. frees once if several buffers share the same memory)
 */
void destroyAllBuffers(VkDevice device, const std::vector<Buffer>& buffers);
