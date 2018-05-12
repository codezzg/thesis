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

	VkDescriptorSet descriptorSet;

	VkPipeline pipeline;

	VkRenderPass renderPass;

	void destroyTransient(VkDevice device);
};

SwapChain createSwapChain(const Application& app, VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);

std::vector<VkImageView> createSwapChainImageViews(const Application& app);

std::vector<VkFramebuffer> createSwapChainFramebuffers(const Application& app);

/** Returns the index of the next swapchain image, or -1 in case of failure.
 *  Will also signal the given semaphore.
 */
uint32_t acquireNextSwapImage(const Application& app, VkSemaphore imageAvailableSemaphore);

std::vector<VkCommandBuffer> createSwapChainCommandBuffers(const Application& app);

void recordSwapChainCommandBuffers(const Application& app, std::vector<VkCommandBuffer>& buffers,
		uint32_t nIndices, const Buffer& uBuffer, VkDescriptorSet descSet);

VkDescriptorPool createSwapChainDescriptorPool(const Application& app);
VkDescriptorSetLayout createSwapChainDescriptorSetLayout(const Application& app);
VkDescriptorSet createSwapChainDescriptorSet(const Application& app, VkDescriptorSetLayout descriptorSetLayout,
		const Buffer& uniformBuffer, const Image& texDiffuse);
VkPipeline createSwapChainPipeline(const Application& app, const std::string& shader = "composition");

VkDescriptorSetLayout createSwapChainDebugDescriptorSetLayout(const Application& app);
VkDescriptorSet createSwapChainDebugDescriptorSet(const Application& app, VkDescriptorSetLayout descriptorSetLayout,
		const Buffer& uniformBuffer, const Image& tex);
std::vector<VkCommandBuffer> createSwapChainDebugCommandBuffers(const Application& app, uint32_t nIndices,
		const Buffer& vertexBuffer, const Buffer& indexBuffer, const Buffer& uniformBuffer,
		VkDescriptorSet descriptorSet);
