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

	GLFWwindow* window = nullptr;

	VkInstance instance = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;

	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;

	Queues queues;

	Validation validation;

	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

	SwapChain swapChain;

	GBuffer gBuffer;

	Buffer screenQuadBuffer;

	Resources res;

	VkPipelineCache pipelineCache = VK_NULL_HANDLE;

	VkRenderPass renderPass = VK_NULL_HANDLE;

	void init();
	void cleanup();
};

VkDescriptorPool createDescriptorPool(const Application& app);