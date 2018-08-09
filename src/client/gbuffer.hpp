#pragma once

#include "images.hpp"
#include <tuple>
#include <vector>
#include <vulkan/vulkan.h>

struct Application;

struct GBuffer {
	Image position;
	Image normal;
	Image albedoSpec;

	void createAttachments(const Application& app);

	void destroy(VkDevice device)
	{
		destroyAllImages(device,
			{
				position,
				normal,
				albedoSpec,
			});
	}
};

/** @return The pipeline for the geometry pass */
VkPipeline createGBufferPipeline(const Application& app);

/** Updates `descriptorSet` with the new G-Buffer attachments.
 *  To be called on swapchain recreation (as the G-Buffer size changes)
 */
void updateGBufferDescriptors(const Application& app, VkDescriptorSet descriptorSet, VkSampler texSampler);
