#include "validation.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>
#include "vulk_errors.hpp"
#ifndef NDEBUG
	#include <sstream>
	#include "utils.hpp"
#endif

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
		uint64_t obj,
		std::size_t /*location*/,
		int32_t /*code*/,
		const char* /*layerPrefix*/,
		const char* msg,
		void* userData)
{
	const auto data = reinterpret_cast<const Validation*>(userData);
	auto& objectsInfo = data->objectsInfo;
	auto it = objectsInfo.find(obj);
	if (it != objectsInfo.end())
		std::cerr << "[Object created near " << it->second << "]\n";
	std::cerr << "validation layer: " << data->addDetails(msg) << "\n" << std::endl;

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

static VkDebugReportCallbackEXT createDebugCallback(VkInstance instance, Validation *validation) {
	VkDebugReportCallbackCreateInfoEXT createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
	createInfo.pfnCallback = debugCallback;
	createInfo.pUserData = validation;

	VkDebugReportCallbackEXT callback;
	VLKCHECK(createDebugReportCallbackEXT(instance, &createInfo, nullptr, &callback));

	return callback;
}


void Validation::requestLayers(const std::vector<const char*>& layers) {
	enabledLayers = layers;
}

void Validation::init(VkInstance _instance) {
	instance = _instance;
	if (enabled())
		debugReportCallback = createDebugCallback(instance, this);
}

void Validation::cleanup() {
	if (debugReportCallback)
		destroyDebugReportCallbackEXT(instance, debugReportCallback, nullptr);
}

bool Validation::enabled() const {
	return enabledLayers.size() > 0;
}

void Validation::addObjectInfo(void *handle, const char *file, int line) const {
#ifndef NDEBUG
	std::stringstream ss;
	ss << file << ":" << line;
	//std::cerr << "added " << std::hex << "0x" << reinterpret_cast<uint64_t>(handle) << " -> " << ss.str() << std::endl;
	objectsInfo[reinterpret_cast<uint64_t>(handle)] = ss.str();
#endif
}

std::string Validation::addDetails(const char *msg) const {
#ifndef NDEBUG
	std::istringstream iss{ msg };
	std::ostringstream oss;
	bool pre = true;

	while (iss) {
		std::string token;
		iss >> token;
		if (token == "|")
			pre = false;

		oss << token << " ";
		// Skip until after "|"
		if (pre)
			continue;

		if (!startsWith(token, "0x"))
			continue;

		const uint64_t info = std::stoul(token, nullptr, 16);
		const auto it = objectsInfo.find(info);
		if (it != objectsInfo.end()) {
			// only keep basename
			const auto idx = it->second.find_last_of("/\\");
			oss << "[[" << it->second.substr(idx + 1) << "]] ";
		}
	}

	return oss.str();
#endif
	return std::string{ msg };
}
