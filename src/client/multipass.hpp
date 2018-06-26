#pragma once

#include "hashing.hpp"
#include <vector>
#include <vulkan/vulkan.h>

struct Application;
struct Buffer;
struct CombinedUniformBuffers;
struct Geometry;
struct Material;
struct NetworkResources;

void recordMultipassCommandBuffers(const Application& app,
	std::vector<VkCommandBuffer>& commandBuffers,
	const Geometry& geometry,
	const NetworkResources& netRsrc);

std::vector<VkDescriptorSetLayout> createMultipassDescriptorSetLayouts(const Application& app);

/** Creates descriptor sets for all the given materials, assigning the proper textures.
 *  @return A vector where each descriptorset correspond to the i-th material in input.
 */
std::vector<VkDescriptorSet> createMultipassDescriptorSets(const Application& app,
	const CombinedUniformBuffers& uniformBuffers,
	const std::vector<Material>& materials,
	VkSampler texSampler,
	VkSampler cubeSampler);
