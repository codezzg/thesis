#include "application.hpp"
#include "phys_device.hpp"
#include "window.hpp"
#include <set>

static VkInstance createInstance(const Validation& validation) {
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Hello Triangle";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	auto extensions = getRequiredExtensions(validation.enabled());
	createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();
	if (validation.enabled())
		validation.enableOn(createInfo);

	VkInstance instance;
	if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
		throw std::runtime_error("failed to create instance!");
	}

	return instance;
}

static VkSurfaceKHR createSurface(VkInstance instance, GLFWwindow *window) {
	VkSurfaceKHR surface;
	if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
		throw std::runtime_error("failed to create window surface!");

	return surface;
}

static void createLogicalDevice(Application& app) {
	QueueFamilyIndices indices = findQueueFamilies(app.physicalDevice, app.surface);

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<int> uniqueQueueFamilies = {
		indices.graphicsFamily,
		indices.presentFamily
	};

	float queuePriority = 1.0f;
	for (int queueFamily : uniqueQueueFamilies) {
		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueCreateInfo);
	}

	VkPhysicalDeviceFeatures deviceFeatures = {};
	deviceFeatures.samplerAnisotropy = VK_TRUE;

	VkDeviceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = static_cast<uint32_t>(gDeviceExtensions.size());
	createInfo.ppEnabledExtensionNames = gDeviceExtensions.data();
	if (app.validation.enabled())
		app.validation.enableOn(createInfo);

	if (vkCreateDevice(app.physicalDevice, &createInfo, nullptr, &app.device) != VK_SUCCESS) {
		throw std::runtime_error("failed to create logical device!");
	}

	vkGetDeviceQueue(app.device, indices.graphicsFamily, 0, &app.queues.graphics);
	vkGetDeviceQueue(app.device, indices.presentFamily, 0, &app.queues.present);
}


void Application::init() {
#ifndef NDEBUG
	const std::vector<const char*> enabledLayers = {
		"VK_LAYER_LUNARG_standard_validation"
	};
	validation.requestLayers(enabledLayers);
#endif
	window = initWindow();
	instance = createInstance(validation);
	validation.init(instance);

	//resources.pipelineLayouts = std::make_unique<PipelineLayoutMap>(device);
	//resources.pipelines = std::make_unique<PipelineMap>(device);
	//resources.descriptorSetLayouts = std::make_unique<DescriptorSetLayoutMap>(device);
	//resources.descriptorSets = std::make_unique<DescriptorSetMap>(device, descriptorPool);

	surface = createSurface(instance, window);
	physicalDevice = pickPhysicalDevice(instance, surface);
	createLogicalDevice(*this);
}

void Application::cleanup() {
	validation.cleanup();
	vkDestroyDevice(device, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyInstance(instance, nullptr);
	cleanupWindow(window);
}
