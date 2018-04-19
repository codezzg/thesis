#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <tuple>
#include "images.hpp"
#include "buffers.hpp"

struct GBuffer final {
	VkFramebuffer handle;
	std::vector<Image> attachments;

	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorPool descriptorPool;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	VkRenderPass renderPass;

	void destroy(VkDevice device) {
		for (auto& img : attachments)
			img.destroy(device);
		vkDestroyFramebuffer(device, handle, nullptr);

		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyRenderPass(device, renderPass, nullptr);
	}
};

struct Application;

/** Creates a position, normal, albedo/spec and depth attachment */
std::vector<Image> createGBufferAttachments(const Application& app);

/** Creates a GBuffer, fills it with the given attachments and returns it. */
GBuffer createGBuffer(const Application& app, const std::vector<Image>& attachments, VkRenderPass renderPass);

/** Creates a DescriptorPool fit for containing the g-buffer's descriptor set */
VkDescriptorPool createGBufferDescriptorPool(VkDevice device);

/** Creates a VkDescriptorSetLayout for the gbuffer shaders. */
VkDescriptorSetLayout createGBufferDescriptorSetLayout(const Application& app);

VkDescriptorSet createGBufferDescriptorSet(const Application& app, VkDescriptorSetLayout descriptorSetLayout,
		const Buffer& uniformBuffer, const Image& texDiffuseImage, const Image& texSpecularImage);

std::pair<VkPipeline, VkPipelineLayout> createGBufferPipeline(const Application& app);

VkCommandBuffer createGBufferCommandBuffer(const Application& app, uint32_t nIndices,
		const Buffer& vBuffer, const Buffer& iBuffer, const Buffer& uBuffer, VkDescriptorSet descSet);
