#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <tuple>
#include "images.hpp"
#include "buffers.hpp"

struct Application;

struct GBuffer final {
	VkFramebuffer framebuffer;

	Image position;
	Image normal;
	Image albedoSpec;
	Image depth;

	VkDescriptorSet descriptorSet;

	VkPipeline pipeline;

	VkRenderPass renderPass;

	void createAttachments(const Application& app);

	void destroyTransient(VkDevice device) {
		position.destroy(device);
		normal.destroy(device);
		albedoSpec.destroy(device);
		depth.destroy(device);

		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyRenderPass(device, renderPass, nullptr);
		vkDestroyFramebuffer(device, framebuffer, nullptr);
	}
};

VkFramebuffer createGBufferFramebuffer(const Application& app);

/** Creates a position, normal, albedo/spec and depth attachment */
std::vector<Image> createGBufferAttachments(const Application& app);

/** Creates a GBuffer, fills it with the given attachments and returns it. */
GBuffer createGBuffer(const Application& app, const std::vector<Image>& attachments);

/** Creates a VkDescriptorSetLayout for the gbuffer shaders. */
VkDescriptorSetLayout createGBufferDescriptorSetLayout(const Application& app);

VkDescriptorSet createGBufferDescriptorSet(const Application& app, VkDescriptorSetLayout descriptorSetLayout,
		const Buffer& uniformBuffer, const Image& texDiffuseImage, const Image& texSpecularImage);

VkPipelineLayout createGBufferPipelineLayout(const Application& app);
VkPipeline createGBufferPipeline(const Application& app);

VkCommandBuffer createGBufferCommandBuffer(const Application& app, uint32_t nIndices,
		const Buffer& vBuffer, const Buffer& iBuffer, const Buffer& uBuffer, VkDescriptorSet descSet);
