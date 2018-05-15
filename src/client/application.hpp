#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "validation.hpp"
#include "swap.hpp"
#include "images.hpp"
#include "memory.hpp"
#include "buffers.hpp"
#include "gbuffer.hpp"
#include "resources.hpp"

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

	VkCommandPool commandPool;
	VkDescriptorPool descriptorPool;

	SwapChain swapChain;
	Image depthImage;

	GBuffer gBuffer;

	Buffer screenQuadBuffer;

	Resources res;

	VkPipelineCache pipelineCache;

	VkRenderPass renderPass;

	void init();
	void cleanup();
};

VkDescriptorPool createDescriptorPool(const Application& app);
