#pragma once

#include "buffers.hpp"
#include "images.hpp"
#include <vector>
#include <vulkan/vulkan.h>

struct Application;

struct Skybox {
	Image image;
	/** Stores [vertices|indices] */
	Buffer buffer;
	VkPipeline pipeline;
	/** Offset (in bytes) of the first index inside `buffer` */
	VkDeviceSize indexOff;
};

/** Creates the skybox (currently with fixed textures) and returns it.
 *  In case of failure, the returned Image will have its handle set to VK_NULL_HANDLE.
 */
Image createSkybox(const Application& app);

std::vector<VkDescriptorSetLayout> createSkyboxDescriptorSetLayouts(const Application& app);
std::vector<VkDescriptorSet> createSkyboxDescriptorSets(const Application& app);
