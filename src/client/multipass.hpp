#pragma once

#include "hashing.hpp"
#include <vector>
#include <vulkan/vulkan.h>

struct Application;
class BufferArray;
struct Buffer;
struct CombinedUniformBuffers;
struct Geometry;
struct Material;
struct ModelInfo;
struct NetworkResources;

void recordMultipassCommandBuffers(const Application& app,
	std::vector<VkCommandBuffer>& commandBuffers,
	const Geometry& geometry,
	const NetworkResources& netRsrc,
	const BufferArray& uniformBuffers);

std::vector<VkDescriptorSetLayout> createMultipassDescriptorSetLayouts(const Application& app);

/** Creates the fixed descriptor sets that don't depend on server resources.
 *  @return A vector containing { viewDescSet, gbufDescSet }
 */
std::vector<VkDescriptorSet> createMultipassPermanentDescriptorSets(const Application& app,
	const BufferArray& uniformBuffers,
	VkSampler texSampler);

/** Creates descriptor sets for all the given materials and models, assigning the proper textures
 *  and uniform buffers.
 *  @return A vector containing all consecutive materials' and models' descriptor sets.
 */
std::vector<VkDescriptorSet> createMultipassTransitoryDescriptorSets(const Application& app,
	const BufferArray& uniformBuffers,
	const std::vector<Material>& materials,
	const std::vector<ModelInfo>& models,
	VkSampler texSampler);
