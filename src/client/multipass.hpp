#pragma once

#include <vector>
#include <vulkan/vulkan.h>

struct Application;
struct Buffer;
struct Image;

void recordMultipassCommandBuffers(const Application& app, std::vector<VkCommandBuffer>& commandBuffers,
		uint32_t nIndices, const Buffer& vBuffer, const Buffer& iBuffer);

VkDescriptorSetLayout createMultipassDescriptorSetLayout(const Application& app);

VkDescriptorSet createMultipassDescriptorSet(const Application& app,
		const Buffer& mvpUbo, const Buffer& compUbo,
		const Image& texDiffuse, const Image& texSpecular, VkSampler texSampler);
