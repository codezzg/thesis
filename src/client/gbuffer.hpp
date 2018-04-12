#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <tuple>
#include "images.hpp"

struct Application;

struct GBuffer final {
	VkFramebuffer handle;
	std::vector<Image> attachments;
	VkDescriptorSetLayout descriptorSetLayout;
};

/** Creates a position, normal, albedo/spec and depth attachment */
std::vector<Image> createGBufferAttachments(const Application& app);

/** Creates a GBuffer, fills it with the given attachments and returns it. */
GBuffer createGBuffer(const Application& app, const std::vector<Image>& attachments);

/** Creates a VkDescriptorSetLayout for the gbuffer shaders. */
VkDescriptorSetLayout createGBufferDescriptorSetLayout(const Application& app);

std::pair<VkPipeline, VkPipelineLayout> createGBufferPipeline(const Application& app);
