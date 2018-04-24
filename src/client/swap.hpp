#pragma once

#include <vector>
#include <tuple>
#include <vulkan/vulkan.h>
#include "buffers.hpp"

struct Application;
struct Image;

struct SwapChain final {
	VkSwapchainKHR handle;
	VkExtent2D extent;
	VkFormat imageFormat;

	std::vector<VkImage> images;
	std::vector<VkImageView> imageViews;
	std::vector<VkFramebuffer> framebuffers;

	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSet;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	VkRenderPass renderPass;

	Buffer screenQuadBuffer;

	void destroyTransient(VkDevice device);
	void destroyPersistent(VkDevice device);
};

SwapChain createSwapChain(const Application& app);

std::vector<VkImageView> createSwapChainImageViews(const Application& app);

std::vector<VkFramebuffer> createSwapChainFramebuffers(const Application& app);

/** Returns the index of the next swapchain image, or -1 in case of failure.
 *  Will also signal the given semaphore.
 */
uint32_t acquireNextSwapImage(const Application& app, VkSemaphore imageAvailableSemaphore);

std::vector<VkCommandBuffer> createSwapChainCommandBuffers(const Application& app, uint32_t nIndices,
		const Buffer& uBuffer, VkDescriptorSet descSet);

VkDescriptorPool createSwapChainDescriptorPool(const Application& app);
VkDescriptorSetLayout createSwapChainDescriptorSetLayout(const Application& app);
VkDescriptorSet createSwapChainDescriptorSet(const Application& app, VkDescriptorSetLayout descriptorSetLayout,
		const Buffer& uniformBuffer);
std::pair<VkPipeline, VkPipelineLayout> createSwapChainPipeline(const Application& app);

VkDescriptorSetLayout createSwapChainDebugDescriptorSetLayout(const Application& app);
VkDescriptorSet createSwapChainDebugDescriptorSet(const Application& app, VkDescriptorSetLayout descriptorSetLayout,
		const Buffer& uniformBuffer);
