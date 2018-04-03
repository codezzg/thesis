#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "validation.hpp"
#include "swap.hpp"
#include "images.hpp"

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

	VkCommandPool commandPool;

	SwapChain swapChain;

	VkRenderPass renderPass;

	Image textureImage;
	Image depthImage;

	void init();
	void cleanup();
};
