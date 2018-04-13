#include "swap.hpp"
#include <limits>
#include <algorithm>
#include <array>
#include "buffers.hpp"
#include "application.hpp"
#include "phys_device.hpp"
#include "vulk_errors.hpp"
#include "images.hpp"

// Windows, really...
#undef max
#undef min

static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
	// No preferred format
	if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED)
		return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	for (const auto& availableFormat : availableFormats) {
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM
				&& availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			return availableFormat;
	}

	return availableFormats[0];
}

static VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
	auto bestMode = VK_PRESENT_MODE_FIFO_KHR;

	for (const auto& availablePresentMode : availablePresentModes) {
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
			return availablePresentMode;
		} else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
			bestMode = availablePresentMode;
		}
	}
	return bestMode;
}

static VkExtent2D chooseSwapExtent(GLFWwindow *window, const VkSurfaceCapabilitiesKHR& capabilities) {
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		return capabilities.currentExtent;
	else {
		int width, height;
		glfwGetWindowSize(window, &width, &height);
		VkExtent2D actualExtent = { uint32_t(width), uint32_t(height) };
		actualExtent.width = std::max(capabilities.minImageExtent.width,
						std::min(capabilities.maxImageExtent.width, actualExtent.width));
		actualExtent.height = std::max(capabilities.minImageExtent.height,
						std::min(capabilities.maxImageExtent.height, actualExtent.height));
		return actualExtent;
	}
}

SwapChain createSwapChain(const Application& app) {
	const auto swapChainSupport = querySwapChainSupport(app.physicalDevice, app.surface);
	const auto surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
	const auto presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
	const auto extent = chooseSwapExtent(app.window, swapChainSupport.capabilities);

	uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
	if (swapChainSupport.capabilities.maxImageCount > 0 &&
			swapChainSupport.capabilities.maxImageCount < imageCount) {
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = app.surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	auto indices = findQueueFamilies(app.physicalDevice, app.surface);
	const std::array<uint32_t, 2> queueFamilyIndices = {
		static_cast<uint32_t>(indices.graphicsFamily),
		static_cast<uint32_t>(indices.presentFamily)
	};

	if (indices.graphicsFamily != indices.presentFamily) {
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = queueFamilyIndices.size();
		createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
	} else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}
	createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;

	VkSwapchainKHR swapChainHandle;
	VLKCHECK(vkCreateSwapchainKHR(app.device, &createInfo, nullptr, &swapChainHandle));

	SwapChain swapChain;
	swapChain.handle = swapChainHandle;
	swapChain.extent = extent;
	swapChain.imageFormat = surfaceFormat.format;

	vkGetSwapchainImagesKHR(app.device, swapChain.handle, &imageCount, nullptr);
	swapChain.images.resize(imageCount);
	vkGetSwapchainImagesKHR(app.device, swapChain.handle, &imageCount, swapChain.images.data());

	return swapChain;
}

std::vector<VkImageView> createSwapChainImageViews(const Application& app) {
	std::vector<VkImageView> imageViews{ app.swapChain.images.size() };

	for (std::size_t i = 0; i < app.swapChain.images.size(); ++i) {
		imageViews[i] = createImageView(app,
			app.swapChain.images[i], app.swapChain.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	return imageViews;
}

std::vector<VkFramebuffer> createSwapChainFramebuffers(const Application& app) {
	std::vector<VkFramebuffer> framebuffers{ app.swapChain.imageViews.size() };

	for (std::size_t i = 0; i < app.swapChain.imageViews.size(); ++i) {
		const std::array<VkImageView, 2> attachments = {
			app.swapChain.imageViews[i],
			app.depthImage.view
		};

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = app.lightRenderPass;
		framebufferInfo.attachmentCount = attachments.size();
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = app.swapChain.extent.width;
		framebufferInfo.height = app.swapChain.extent.height;
		framebufferInfo.layers = 1;

		VLKCHECK(vkCreateFramebuffer(app.device, &framebufferInfo, nullptr, &framebuffers[i]));
	}

	return framebuffers;
}

uint32_t acquireNextSwapImage(const Application& app, VkSemaphore imageAvailableSemaphore) {
	uint32_t imageIndex;
	const auto result = vkAcquireNextImageKHR(app.device, app.swapChain.handle,
			std::numeric_limits<uint64_t>::max(),
			imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		return -1;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		throw std::runtime_error("failed to acquire swap chain image!");
	}

	return imageIndex;
}

std::vector<VkCommandBuffer> createSwapChainCommandBuffers(const Application& app, uint32_t nIndices,
		const Buffer& vertexBuffer, const Buffer& indexBuffer, const Buffer& uniformBuffer,
		VkDescriptorSet descriptorSet)
{
	std::vector<VkCommandBuffer> commandBuffers{ app.swapChain.framebuffers.size() };

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = app.commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

	VLKCHECK(vkAllocateCommandBuffers(app.device, &allocInfo, commandBuffers.data()));

	for (size_t i = 0; i < commandBuffers.size(); ++i) {
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		beginInfo.pInheritanceInfo = nullptr;

		vkBeginCommandBuffer(commandBuffers[i], &beginInfo);

		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = app.geomRenderPass;
		renderPassInfo.framebuffer = app.swapChain.framebuffers[i];
		renderPassInfo.renderArea.offset = {0, 0};
		renderPassInfo.renderArea.extent = app.swapChain.extent;
		std::array<VkClearValue, 2> clearValues = {};
		clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
		clearValues[1].depthStencil = {1.f, 0};
		renderPassInfo.clearValueCount = clearValues.size();
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, app.graphicsPipeline);
		const std::array<VkBuffer, 1> vertexBuffers = { vertexBuffer.handle };
		const std::array<VkDeviceSize, 1> offsets = { 0 };
		static_assert(vertexBuffers.size() == offsets.size(),
				"offsets should be the same amount of vertexBuffers!");
		vkCmdBindVertexBuffers(commandBuffers[i], 0, vertexBuffers.size(),
				vertexBuffers.data(), offsets.data());
		vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
		vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
				app.graphicsPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
		vkCmdDrawIndexed(commandBuffers[i], nIndices, 1, 0, 0, 0);
		//std::cerr << "recreating command buffer with v = "
			//<< nVertices << ", i = " << nIndices << "\n";
		vkCmdEndRenderPass(commandBuffers[i]);

		VLKCHECK(vkEndCommandBuffer(commandBuffers[i]));
	}

	return commandBuffers;
}
