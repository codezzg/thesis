#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "validation.hpp"

struct Queues final {
	VkQueue graphics;
	VkQueue present;
};


struct Application final {

	GLFWwindow* window;

	VkInstance instance;
	VkSurfaceKHR surface;

	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device;

	Queues queues;

	Validation validation;


	void init();
	void cleanup();
};
