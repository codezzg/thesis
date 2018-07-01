#pragma once

#include <tuple>
#include <vector>
#include <vulkan/vulkan.h>

struct Application;

struct Image final {
	VkImage handle = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize offset;   // offset into underlying device memory
	VkImageView view = VK_NULL_HANDLE;
	VkFormat format;
};

/** Use this class to allocate a bunch of images at once.
 *  This allocator will attempt to minimize the number of allocations by reusing the same memory
 *  for multiple images with proper offsets.
 */
class ImageAllocator final {
	std::vector<VkImageCreateInfo> createInfos;
	std::vector<VkMemoryPropertyFlags> properties;
	std::vector<Image*> images;

public:
	/** Schedules a new image to be created and binds it to `image`. */
	void addImage(Image& image,
		uint32_t width,
		uint32_t height,
		VkFormat format,
		VkImageTiling tiling,
		VkImageUsageFlags usage,
		VkMemoryPropertyFlags properties,
		VkImageCreateFlags flags = 0,
		uint32_t arrayLayers = 1);

	/** Creates the scheduled buffers and allocates their memory. */
	void create(const Application& app);
};

VkImageView createImageView(const Application& app, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
VkImageView createImageCubeView(const Application& app, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

/** Creates a new image. The returned Image will NOT have a view attached.
 *  Note: prefer allocating many images at once using ImageAllocator.
 */
Image createImage(const Application& app,
	uint32_t width,
	uint32_t height,
	VkFormat format,
	VkImageTiling tiling,
	VkImageUsageFlags usage,
	VkMemoryPropertyFlags properties,
	VkImageCreateFlags flags = 0,
	uint32_t arrayLayers = 1);

/** Transitions `image` from `oldLayout` to `newLayout`.
 *  `subresourceRange` is used to specify the number of layers and mip levels of the image.
 *  There is no need to specify the aspectMask, as this is automatically set by this function.
 */
void transitionImageLayout(const Application& app,
	VkImage image,
	VkFormat format,
	VkImageLayout oldLayout,
	VkImageLayout newLayout,
	VkImageSubresourceRange subresourceRange);

Image createDepthImage(const Application& app);

void destroyImage(VkDevice, Image image);

/** @see destroyAllBuffers */
void destroyAllImages(VkDevice device, const std::vector<Image>& images);
