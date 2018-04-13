#pragma once

#include <vector>
#include <vulkan/vulkan.h>

struct Application;
struct Buffer;

struct SwapChain final {
	VkSwapchainKHR handle;
	VkExtent2D extent;
	VkFormat imageFormat;
	std::vector<VkImage> images;
	std::vector<VkImageView> imageViews;
	std::vector<VkFramebuffer> framebuffers;
};

SwapChain createSwapChain(const Application& app);

std::vector<VkImageView> createSwapChainImageViews(const Application& app);

std::vector<VkFramebuffer> createSwapChainFramebuffers(const Application& app);

/** Returns the index of the next swapchain image, or -1 in case of failure.
 *  Will also signal the given semaphore.
 */
uint32_t acquireNextSwapImage(const Application& app, VkSemaphore imageAvailableSemaphore);

std::vector<VkCommandBuffer> createSwapChainCommandBuffers(const Application& app, uint32_t nIndices,
		const Buffer& vBuffer, const Buffer& iBuffer, const Buffer& uBuffer, VkDescriptorSet descSet);
