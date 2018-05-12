#pragma once

#include <vector>
#include <vulkan/vulkan.h>

struct Application;

VkPipelineLayout createPipelineLayout(const Application& app, VkDescriptorSetLayout descSetLayout,
		const std::vector<VkPushConstantRange>& pushConstantRanges = {});
