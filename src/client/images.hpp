#pragma once

#include <vulkan/vulkan.h>

struct Application;

struct Image final {
	VkImage handle;
	VkDeviceMemory memory;
	VkImageView view;
	VkFormat format;

	void destroy(VkDevice device) {
		vkDestroyImageView(device, view, nullptr);
		vkDestroyImage(device, handle, nullptr);
		vkFreeMemory(device, memory, nullptr);
	}
};

VkImageView createImageView(const Application& app,
		VkImage image,
		VkFormat format,
		VkImageAspectFlags aspectFlags);

/** Creates a new image. The returned Image will NOT have valid `view` and `sampler` handles, as they're optional
 *  and are set externally.
 */
Image createImage(
		const Application& app,
		uint32_t width,
		uint32_t height,
		VkFormat format,
		VkImageTiling tiling,
		VkImageUsageFlags usage,
		VkMemoryPropertyFlags properties);

void transitionImageLayout(const Application& app,
		VkImage image, VkFormat format,
		VkImageLayout oldLayout, VkImageLayout newLayout);

Image createDepthImage(const Application& app);
