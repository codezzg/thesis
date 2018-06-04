#include "formats.hpp"
#include <stdexcept>

VkFormat formats::depth;
VkFormat formats::position;
VkFormat formats::normal;
VkFormat formats::albedoSpec;

VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice,
        const std::vector<VkFormat>& candidates,
        VkImageTiling tiling,
        VkFormatFeatureFlags features) {
	for (auto format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
			return format;
		} else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}

	throw std::runtime_error("failed to find supported format!");
}

static VkFormat findDepthFormat(VkPhysicalDevice physicalDevice) {
	return findSupportedFormat(physicalDevice,
	        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
	        VK_IMAGE_TILING_OPTIMAL,
	        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

static VkFormat findNormalFormat(VkPhysicalDevice physicalDevice) {
	return findSupportedFormat(physicalDevice,
	        {
	                VK_FORMAT_R32G32B32_SFLOAT,
	                VK_FORMAT_R32G32B32A32_SFLOAT,
	        },
	        VK_IMAGE_TILING_OPTIMAL,
	        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
}

static VkFormat findPositionFormat(VkPhysicalDevice physicalDevice) {
	return findSupportedFormat(physicalDevice,
	        {
	                VK_FORMAT_R32G32B32_SFLOAT,
	                VK_FORMAT_R32G32B32A32_SFLOAT,
	        },
	        VK_IMAGE_TILING_OPTIMAL,
	        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
}

static VkFormat findAlbedoSpecFormat(VkPhysicalDevice physicalDevice) {
	return findSupportedFormat(physicalDevice,
	        {
	                VK_FORMAT_R8G8B8A8_UNORM,
	        },
	        VK_IMAGE_TILING_OPTIMAL,
	        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
}

void findBestFormats(VkPhysicalDevice physicalDevice) {
	formats::depth = findDepthFormat(physicalDevice);
	formats::position = findPositionFormat(physicalDevice);
	formats::normal = findNormalFormat(physicalDevice);
	formats::albedoSpec = findAlbedoSpecFormat(physicalDevice);
}
