#pragma once

#include "buffers.hpp"
#include "images.hpp"
#include <tuple>
#include <vector>
#include <vulkan/vulkan.h>

struct Application;

struct GBuffer final {
	Image position;
	Image normal;
	Image albedoSpec;
	// Image depth;

	VkDescriptorSet descriptorSet;

	// TODO: for now this is owned by gbuffer, as ResourceMap has not way to
	// remove an element. In future, add that method and make this pipeline
	// owned by app.res.
	VkPipeline pipeline;

	void createAttachments(const Application& app);

	void destroyTransient(VkDevice device)
	{
		destroyAllImages(device,
			{
				position,
				normal,
				albedoSpec,
			});

		vkDestroyPipeline(device, pipeline, nullptr);
	}
};

/** @return The pipeline for the geometry pass */
VkPipeline createGBufferPipeline(const Application& app);

/** Updates `descriptorSet` with the new G-Buffer attachments.
 *  To be called on swapchain recreation (as the G-Buffer size changes)
 */
void updateGBufferDescriptors(const Application& app, VkDescriptorSet descriptorSet, VkSampler texSampler);
