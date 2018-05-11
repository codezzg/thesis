#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include "vulk_errors.hpp"
#include "hashing.hpp"


template <typename T>
class ResourceMap {
protected:
	std::unordered_map<StringId, T> resources;
	VkDevice device;

public:
	explicit ResourceMap(VkDevice device) : device(device) {}
	virtual ~ResourceMap() {}

	T& operator[](const StringId& name) {
		return resources[name];
	}
	T& operator[](const char *name) { return operator[](sid(name)); }

	T& get(const StringId& name) {
		auto it = resources.find(name);
		if (it == resources.end())
			throw std::runtime_error("Couldn't find resource: " + sidToString(name));
		return it->second;
	}
	T& get(const char *name) { return get(sid(name)); }

	void add(const StringId& name, T rsrc) {
		resources[name] = rsrc;
	}
	void add(const char *name, T rsrc) { add(sid(name), rsrc); }
};

class PipelineLayoutMap : public ResourceMap<VkPipelineLayout> {
public:
	explicit PipelineLayoutMap(VkDevice device) : ResourceMap(device) {}
	~PipelineLayoutMap() {
		for (auto& pair : resources)
			vkDestroyPipelineLayout(device, pair.second, nullptr);
	}

	VkPipelineLayout create(const StringId& name, const VkPipelineLayoutCreateInfo& createInfo) {
		VkPipelineLayout rsrc;
		VLKCHECK(vkCreatePipelineLayout(device, &createInfo, nullptr, &rsrc));
		resources[name] = rsrc;
		return rsrc;
	}
	VkPipelineLayout create(const char *name, const VkPipelineLayoutCreateInfo& createInfo) {
		return create(sid(name), createInfo);
	}
};

class DescriptorSetLayoutMap : public ResourceMap<VkDescriptorSetLayout> {
public:
	explicit DescriptorSetLayoutMap(VkDevice device) : ResourceMap(device) {}
	~DescriptorSetLayoutMap() {
		for (auto& pair : resources)
			vkDestroyDescriptorSetLayout(device, pair.second, nullptr);
	}

	VkDescriptorSetLayout create(const StringId& name, const VkDescriptorSetLayoutCreateInfo& createInfo) {
		VkDescriptorSetLayout rsrc;
		VLKCHECK(vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &rsrc));
		resources[name] = rsrc;
		return rsrc;
	}
	VkDescriptorSetLayout create(const char *name, const VkDescriptorSetLayoutCreateInfo& createInfo) {
		return create(sid(name), createInfo);
	}
};

class PipelineMap : public ResourceMap<VkPipeline> {
public:
	explicit PipelineMap(VkDevice device) : ResourceMap(device) {}
	~PipelineMap() {
		for (auto& pair : resources)
			vkDestroyPipeline(device, pair.second, nullptr);
	}

	VkPipeline create(const StringId& name, const VkGraphicsPipelineCreateInfo& createInfo) {
		VkPipeline pipeline;
		VLKCHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline));
		resources[name] = pipeline;
		return pipeline;
	}
	VkPipeline create(const char *name, const VkGraphicsPipelineCreateInfo& createInfo) {
		return create(sid(name), createInfo);
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

	VkDescriptorSet create(const StringId& name, const VkDescriptorSetAllocateInfo& allocInfo) {
		VkDescriptorSet descriptorSet;
		VLKCHECK(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
		resources[name] = descriptorSet;
		return descriptorSet;
	}
	VkDescriptorSet create(const char *name, const VkDescriptorSetAllocateInfo& allocInfo) {
		return create(sid(name), allocInfo);
	}
};

struct Resources {
	PipelineLayoutMap *pipelineLayouts;
	PipelineMap *pipelines;
	DescriptorSetLayoutMap *descriptorSetLayouts;
	DescriptorSetMap *descriptorSets;

	void init(VkDevice device, VkDescriptorPool descriptorPool) {
		pipelineLayouts = new PipelineLayoutMap(device);
		pipelines = new PipelineMap(device);
		descriptorSetLayouts = new DescriptorSetLayoutMap(device);
		descriptorSets = new DescriptorSetMap(device, descriptorPool);
	}

	void cleanup() {
		delete pipelines;
		delete pipelineLayouts;
		delete descriptorSets;
		delete descriptorSetLayouts;
	}
};
