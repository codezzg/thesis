#pragma once

#include <vector>
#include <vulkan/vulkan.h>

struct Application;

namespace shared {
struct SpirvShader;
}

VkPipelineLayout createPipelineLayout(const Application& app,
	const std::vector<VkDescriptorSetLayout>& descSetLayout,
	const std::vector<VkPushConstantRange>& pushConstantRanges = {});

VkPipelineCache createPipelineCache(const Application& app);

std::vector<VkPipeline> createPipelines(const Application& app, const std::vector<shared::SpirvShader>& shaders);
