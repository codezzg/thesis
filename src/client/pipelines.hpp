#pragma once

#include <vector>
#include <vulkan/vulkan.h>

struct Application;

VkPipelineLayout createPipelineLayout(const Application& app,
	const std::vector<VkDescriptorSetLayout>& descSetLayout,
	const std::vector<VkPushConstantRange>& pushConstantRanges = {});

VkPipelineCache createPipelineCache(const Application& app);
