#pragma once

#include "buffers.hpp"
#include "images.hpp"
#include <string>
#include <tuple>
#include <vector>
#include <vulkan/vulkan.h>

struct Application;
struct Geometry;
struct NetworkResources;

struct SwapChain final {
	VkSwapchainKHR handle = VK_NULL_HANDLE;
	VkExtent2D extent;
	VkFormat imageFormat;

	std::vector<VkImage> images;
	std::vector<VkImageView> imageViews;
	std::vector<VkFramebuffer> framebuffers;
	Image depthImage;
	// VkImageView depthOnlyView; // view of depthImage without the stencil

	VkDescriptorSet descriptorSet;

	// @see comment of gbuffer.pipeline.
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
std::vector<VkFramebuffer> createSwapChainMultipassFramebuffers(const Application& app, const SwapChain& swapChain);

/** Fills `index` with the index of the next swapchain image.
 *  Will also signal the given semaphore.
 *  @return true if all ok, false if the swapchain is out of date.
 */
bool acquireNextSwapImage(const Application& app, VkSemaphore imageAvailableSemaphore, uint32_t& index);

std::vector<VkCommandBuffer> createSwapChainCommandBuffers(const Application& app, VkCommandPool commandPool);

VkPipeline createSwapChainPipeline(const Application& app);
