#pragma once

#include <vector>
#include <tuple>
#include <vulkan/vulkan.h>
#include <string>
#include "buffers.hpp"
#include "images.hpp"

struct Application;

struct SwapChain final {
	VkSwapchainKHR handle = VK_NULL_HANDLE;
	VkExtent2D extent;
	VkFormat imageFormat;

	std::vector<VkImage> images;
	std::vector<VkImageView> imageViews;
	std::vector<VkFramebuffer> framebuffers;
	Image depthImage;
	//VkImageView depthOnlyView; // view of depthImage without the stencil

	VkDescriptorSet descriptorSet;

	VkPipeline pipeline;

	void destroyTransient(VkDevice device);
};

/** Creates a new SwapChain object and fills its `images`, `extent` and `imageFormat`.
 *  Does NOT create other resources, like its imageViews, depthImage or framebuffers.
 */
SwapChain createSwapChain(const Application& app, VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);

/** Given a swapChain, returns a vector with the ImageViews corresponding to its images. */
std::vector<VkImageView> createSwapChainImageViews(const Application& app, const SwapChain& swapChain);

/** Given a swapChain, returns a vector with the Framebuffers with its images attached.
 *  NOTE: app.renderPass must be valid before calling this function.
 */
std::vector<VkFramebuffer> createSwapChainFramebuffers(const Application& app, const SwapChain& swapChain);

/** @see createSwapChainFramebuffers */
std::vector<VkFramebuffer> createSwapChainMultipassFramebuffers(const Application& app, const SwapChain& swapChain);

/** Returns the index of the next swapchain image, or -1 in case of failure.
 *  Will also signal the given semaphore.
 */
uint32_t acquireNextSwapImage(const Application& app, VkSemaphore imageAvailableSemaphore);

std::vector<VkCommandBuffer> createSwapChainCommandBuffers(const Application& app, VkCommandPool commandPool);

VkPipeline createSwapChainPipeline(const Application& app);

/** @return a descriptorSetLayout suited for forward rendering */
VkDescriptorSetLayout createSwapChainDebugDescriptorSetLayout(const Application& app);

/** @return a descriptorSet suited for forward rendering */
VkDescriptorSet createSwapChainDebugDescriptorSet(const Application& app,
		const Buffer& uniformBuffer, const Image& tex, VkSampler texSampler);

/** records a vector of commandBuffers that perform forward rendering */
void recordSwapChainDebugCommandBuffers(const Application& app, std::vector<VkCommandBuffer>& buffers,
		uint32_t nIndices, const Buffer& vertexBuffer, const Buffer& indexBuffer);

/** @return a pipeline suited for forward rendering */
VkPipeline createSwapChainDebugPipeline(const Application& app);
