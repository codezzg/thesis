#include "validation.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>

bool checkValidationLayerSupport(const std::vector<const char*>& requestedLayers) {
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	for (const auto layerName : requestedLayers) {
		if (std::none_of(availableLayers.begin(), availableLayers.end(),
			[layerName] (const auto& layerProperties)
		{
			return strcmp(layerProperties.layerName, layerName) == 0;
		}))
		{
			return false;
		}
	}
	return true;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugReportFlagsEXT /*flags*/,
		VkDebugReportObjectTypeEXT /*objType*/,
		uint64_t /*obj*/,
		size_t /*location*/,
		int32_t /*code*/,
		const char* /*layerPrefix*/,
		const char* msg,
		void* /*userData*/)
{
	std::cerr << "validation layer: " << msg << std::endl;

	return VK_FALSE;
}

static VkResult createDebugReportCallbackEXT(VkInstance instance,
		const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback)
{
	auto func = (PFN_vkCreateDebugReportCallbackEXT)
		vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
	if (func != nullptr)
		return func(instance, pCreateInfo, pAllocator, pCallback);
	else
		return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void destroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback,
		const VkAllocationCallbacks* pAllocator)
{
	auto func = (PFN_vkDestroyDebugReportCallbackEXT)
		vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
	if (func != nullptr)
		func(instance, callback, pAllocator);
}

static VkDebugReportCallbackEXT createDebugCallback(VkInstance instance) {
	VkDebugReportCallbackCreateInfoEXT createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
	createInfo.pfnCallback = debugCallback;

	VkDebugReportCallbackEXT callback;
	if (createDebugReportCallbackEXT(instance, &createInfo, nullptr, &callback) != VK_SUCCESS)
		throw std::runtime_error("failed to set up debug callback!");

	return callback;
}


void Validation::requestLayers(const std::vector<const char*>& layers) {
	enabledLayers = layers;
}

void Validation::init(VkInstance _instance) {
	instance = _instance;
	debugReportCallback = createDebugCallback(instance);
}

void Validation::cleanup() {
	destroyDebugReportCallbackEXT(instance, debugReportCallback, nullptr);
}

bool Validation::enabled() const {
	return enabledLayers.size() > 0;
}
