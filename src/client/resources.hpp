#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <memory>
#include "vulk_errors.hpp"

// TODO: replace with string ID
using SID = std::string;
#define sid(x) x

template <typename T>
class ResourceMap {
protected:
	std::unordered_map<SID, T> resources;
	VkDevice device;

public:
	explicit ResourceMap(VkDevice device) : device(device) {}
	virtual ~ResourceMap() {}

	T& operator[](const SID& name) {
		return resources[name];
	}

	auto find(const SID& name) const {
		return resources.find(name);
	}

	void add(const SID& name, T rsrc) {
		resources[name] = rsrc;
	}
};

#define DEF_RESOURCE_MAP(type) \
	class type##Map : public ResourceMap<Vk##type> { \
	public: \
		explicit type##Map(VkDevice device) : ResourceMap(device) {} \
		~type##Map() { \
			for (auto& pair : resources) \
				vkDestroy##type(device, pair.second, nullptr); \
		} \
		\
		Vk##type create(const SID& name, const Vk##type##CreateInfo& createInfo) { \
			Vk##type rsrc; \
			VLKCHECK(vkCreate##type(device, &createInfo, nullptr, &rsrc)); \
			resources[name] = rsrc; \
			return rsrc; \
		} \
	}

DEF_RESOURCE_MAP(PipelineLayout);
DEF_RESOURCE_MAP(DescriptorSetLayout);

#undef DEF_RESOURCE_MAP

class PipelineMap : public ResourceMap<VkPipeline> {
public:
	explicit PipelineMap(VkDevice device) : ResourceMap(device) {}
	~PipelineMap() {
		for (auto& pair : resources)
			vkDestroyPipeline(device, pair.second, nullptr);
	}

	VkPipeline create(const SID& name, const VkGraphicsPipelineCreateInfo& createInfo) {
		VkPipeline pipeline;
		VLKCHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline));
		resources[name] = pipeline;
		return pipeline;
	}
};

class DescriptorSetMap : public ResourceMap<VkDescriptorSet> {
	const VkDescriptorPool& descriptorPool;
public:
	explicit DescriptorSetMap(VkDevice device, VkDescriptorPool& pool)
		: ResourceMap(device)
		, descriptorPool(pool)
	{}

	~DescriptorSetMap() {
		for (auto& pair : resources)
			vkFreeDescriptorSets(device, descriptorPool, 1, &pair.second);
	}

	VkDescriptorSet create(const SID& name, const VkDescriptorSetAllocateInfo& allocInfo) {
		VkDescriptorSet descriptorSet;
		VLKCHECK(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
		resources[name] = descriptorSet;
		return descriptorSet;
	}
};

struct Resources {
	std::unique_ptr<PipelineLayoutMap> pipelineLayouts;
	std::unique_ptr<PipelineMap> pipelines;
	std::unique_ptr<DescriptorSetLayoutMap> descriptorSetLayouts;
	std::unique_ptr<DescriptorSetMap> descriptorSets;
};
