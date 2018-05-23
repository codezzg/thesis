#include "phys_device.hpp"
#include <unordered_set>
#include "vulk_utils.hpp"
#include "vulk_errors.hpp"

const std::vector<const char*> gDeviceExtensions = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice physDevice, VkSurfaceKHR surface) {
	QueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &queueFamilyCount, queueFamilies.data());

	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphicsFamily = i;
		}

		VkBool32 presentSupport = false;
		VLKCHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physDevice, i, surface, &presentSupport));
		if (queueFamily.queueCount > 0 && presentSupport) {
			indices.presentFamily = i;
		}

		if (indices.isComplete()) {
			break;
		}

		i++;
	}

	return indices;
}

SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice physDevice, VkSurfaceKHR surface) {
	SwapChainSupportDetails details;
	VLKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice, surface, &details.capabilities));

	uint32_t formatCount;
	VLKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &formatCount, nullptr));

	if (formatCount != 0) {
		details.formats.resize(formatCount);
		VLKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &formatCount, details.formats.data()));
	}

	uint32_t presentModeCount;
	VLKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, surface, &presentModeCount, nullptr));

	if (presentModeCount != 0) {
		details.presentModes.resize(presentModeCount);
		VLKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, surface,
				&presentModeCount, details.presentModes.data()));
	}

	return details;
}

bool isDeviceSuitable(VkPhysicalDevice physDevice, VkSurfaceKHR surface) {
	QueueFamilyIndices indices = findQueueFamilies(physDevice, surface);

	bool extensionsSupported = checkDeviceExtensionSupport(physDevice);

	bool swapChainAdequate = false;
	if (extensionsSupported) {
		SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physDevice, surface);
		swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
	}

	VkPhysicalDeviceFeatures supportedFeatures;
	vkGetPhysicalDeviceFeatures(physDevice, &supportedFeatures);

	return indices.isComplete() && extensionsSupported &&
		swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice physDevice) {
	uint32_t extensionCount;
	VLKCHECK(vkEnumerateDeviceExtensionProperties(physDevice, nullptr, &extensionCount, nullptr));

	std::vector<VkExtensionProperties> availableExtensions(extensionCount);
	VLKCHECK(vkEnumerateDeviceExtensionProperties(physDevice, nullptr, &extensionCount, availableExtensions.data()));

	std::unordered_set<std::string> requiredExtensions(gDeviceExtensions.begin(), gDeviceExtensions.end());

	for (const auto& extension : availableExtensions)
		requiredExtensions.erase(extension.extensionName);

	return requiredExtensions.empty();
}

VkPhysicalDevice pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface) {
	uint32_t deviceCount = 0;
	VLKCHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));

	if (deviceCount == 0) {
		throw std::runtime_error("failed to find GPUs with Vulkan support!");
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);
	VLKCHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));

	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	for (const auto& device : devices) {
		if (isDeviceSuitable(device, surface)) {
			physicalDevice = device;
			break;
		}
	}

	dumpPhysicalDevice(physicalDevice);

	if (physicalDevice == VK_NULL_HANDLE) {
		throw std::runtime_error("failed to find a suitable GPU!");
	}

	return physicalDevice;
}

uint32_t findMemoryType(VkPhysicalDevice physDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physDevice, &memProperties);
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
		if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			return i;

	throw std::runtime_error("failed to find suitable memory type!");
}

VkDeviceSize findMinUboAlign(VkPhysicalDevice physDevice) {
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physDevice, &props);
	return props.limits.minUniformBufferOffsetAlignment;
}