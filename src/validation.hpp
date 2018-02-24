#pragma once

#include <vector>
#include <vulkan/vulkan.h>

#ifdef NDEBUG
constexpr bool gEnableValidationLayers = false;
#else
constexpr bool gEnableValidationLayers = true;
#endif

const std::vector<const char*> gValidationLayers = {
	"VK_LAYER_LUNARG_standard_validation"
};

VkResult createDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback);

void destroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback,
		const VkAllocationCallbacks* pAllocator);

bool checkValidationLayerSupport();
