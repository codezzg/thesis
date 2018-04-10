#include "swap.hpp"
#include <limits>
#include <algorithm>
#include <array>
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

void createSwapChainImageViews(Application& app) {
	app.swapChain.imageViews.resize(app.swapChain.images.size());
	for (std::size_t i = 0; i < app.swapChain.images.size(); ++i) {
		app.swapChain.imageViews[i] = createImageView(app,
			app.swapChain.images[i], app.swapChain.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
	}
}

void createSwapChainFramebuffers(Application& app) {
	app.swapChain.framebuffers.resize(app.swapChain.imageViews.size());
	for (std::size_t i = 0; i < app.swapChain.imageViews.size(); ++i) {
		const std::array<VkImageView, 2> attachments = {
			app.swapChain.imageViews[i],
			app.depthImage.view
		};

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = app.geomRenderPass; // FIXME
		framebufferInfo.attachmentCount = attachments.size();
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = app.swapChain.extent.width;
		framebufferInfo.height = app.swapChain.extent.height;
		framebufferInfo.layers = 1;

		VLKCHECK(vkCreateFramebuffer(app.device, &framebufferInfo, nullptr,
					&app.swapChain.framebuffers[i]));
	}
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
