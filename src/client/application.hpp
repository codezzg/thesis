#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "validation.hpp"
#include "swap.hpp"
#include "images.hpp"
#include "memory.hpp"
#include "gbuffer.hpp"
//#include "resources.hpp"

struct Queues final {
	VkQueue graphics;
	VkQueue present;
};

struct Application final {

	//ApplicationMemory memory;

	GLFWwindow* window;

	VkInstance instance;
	VkSurfaceKHR surface;

	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device;

	Queues queues;

	Validation validation;

	VkDescriptorPool descriptorPool;
	VkCommandPool commandPool;

	SwapChain swapChain;

	GBuffer gBuffer;

	VkRenderPass geomRenderPass;
	VkRenderPass lightRenderPass;

	Image depthImage;

	VkPipeline graphicsPipeline;
	VkPipelineLayout graphicsPipelineLayout;

	void init();
	void cleanup();
};
