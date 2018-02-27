#pragma once

#include <vulkan/vulkan.h>
#include <vector>

extern const std::vector<const char*> gDeviceExtensions;

struct QueueFamilyIndices final {
	int graphicsFamily = -1;
	int presentFamily = -1;

	bool isComplete() const {
		return graphicsFamily >= 0 && presentFamily >= 0;
	}
};

struct SwapChainSupportDetails final {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};


QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);

SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);

bool checkDeviceExtensionSupport(VkPhysicalDevice device);
