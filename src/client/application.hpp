#pragma once

#include "buffers.hpp"
#include "gbuffer.hpp"
#include "images.hpp"
#include "resources.hpp"
#include "skybox.hpp"
#include "swap.hpp"
#include "validation.hpp"
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

struct NetworkResources;

struct Queues final {
	VkQueue graphics;
	VkQueue present;
};

struct Application final {

	GLFWwindow* window = nullptr;
	GLFWmonitor* monitor = nullptr;

	VkInstance instance = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;

	Validation validation;

	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;

	Queues queues;
	std::vector<VkCommandBuffer> commandBuffers;

	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

	SwapChain swapChain;
	GBuffer gBuffer;
	Buffer screenQuadBuffer;
	Skybox skybox;

	VkSampler texSampler = VK_NULL_HANDLE;
	VkSampler cubeSampler = VK_NULL_HANDLE;

	Resources res;

	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	VkRenderPass renderPass = VK_NULL_HANDLE;

	void init();
	void cleanup();
};

VkDescriptorPool createDescriptorPool(const Application& app);
