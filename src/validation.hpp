#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <stdexcept>

bool checkValidationLayerSupport(const std::vector<const char*>& requestedLayers);

struct Validation final {
private:
	VkDebugReportCallbackEXT debugReportCallback;
	VkInstance instance;

public:
	std::vector<const char*> enabledLayers;

	void requestLayers(const std::vector<const char*>& layers);
	void init(VkInstance instance);
	void cleanup();
	bool enabled() const;

	template<typename T>
	void enableOn(T& createInfo) const {
		if (!checkValidationLayerSupport(enabledLayers))
			throw std::runtime_error("validation layers requested, but not available!");

		createInfo.enabledLayerCount = enabledLayers.size();
		createInfo.ppEnabledLayerNames = enabledLayers.data();
	}
};

//VkResult createDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
		//const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback);

//void destroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback,
		//const VkAllocationCallbacks* pAllocator);

//VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType,
	//uint64_t obj, size_t location, int32_t code, const char* layerPrefix, const char* msg, void* userData);
