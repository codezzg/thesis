#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <tuple>
#include "images.hpp"
#include "buffers.hpp"

struct Application;

struct GBuffer final {
	Image position;
	Image normal;
	Image albedoSpec;
	Image depth;

	VkDescriptorSet descriptorSet;

	VkPipeline pipeline;

	void createAttachments(const Application& app);

	void destroyTransient(VkDevice device) {
		position.destroy(device);
		normal.destroy(device);
		albedoSpec.destroy(device);
		depth.destroy(device);

		vkDestroyPipeline(device, pipeline, nullptr);
	}
};

/** @return The pipeline for the geometry pass */
VkPipeline createGBufferPipeline(const Application& app);
