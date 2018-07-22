#pragma once

#include "hashing.hpp"
#include "logging.hpp"
#include "vulk_errors.hpp"
#include <unordered_map>
#include <vulkan/vulkan.h>

template <typename T>
class ResourceMap {
protected:
	std::unordered_map<StringId, T> resources;
	VkDevice device;
	std::string resourceType = "resource";

public:
	explicit ResourceMap(VkDevice device)
		: device(device)
	{}
	virtual ~ResourceMap() {}

	T& operator[](const StringId& name) { return resources[name]; }
	T& operator[](const char* name) { return operator[](sid(name)); }

	T& get(const StringId& name)
	{
		auto it = resources.find(name);
		if (it == resources.end())
			throw std::runtime_error("Couldn't find " + resourceType + ": " + sidToString(name));
		return it->second;
	}
	T& get(const char* name) { return get(sid(name)); }

	void add(const StringId& name, T rsrc)
	{
		if (resources.count(name) > 0)
			logging::warn("Warning: overwriting " + resourceType + " ", name);
		resources[name] = rsrc;
	}
	void add(const char* name, T rsrc) { add(sid(name), rsrc); }

	std::size_t size() const { return resources.size(); }
};

class PipelineLayoutMap final : public ResourceMap<VkPipelineLayout> {
public:
	explicit PipelineLayoutMap(VkDevice device)
		: ResourceMap{ device }
	{
		resourceType = "pipelineLayout";
	}

	~PipelineLayoutMap()
	{
		for (auto& pair : resources)
			vkDestroyPipelineLayout(device, pair.second, nullptr);
	}

	VkPipelineLayout create(const StringId& name, const VkPipelineLayoutCreateInfo& createInfo)
	{
		VkPipelineLayout rsrc;
		VLKCHECK(vkCreatePipelineLayout(device, &createInfo, nullptr, &rsrc));
		resources[name] = rsrc;
		return rsrc;
	}
	VkPipelineLayout create(const char* name, const VkPipelineLayoutCreateInfo& createInfo)
	{
		return create(sid(name), createInfo);
	}
};

class DescriptorSetLayoutMap final : public ResourceMap<VkDescriptorSetLayout> {
public:
	explicit DescriptorSetLayoutMap(VkDevice device)
		: ResourceMap{ device }
	{
		resourceType = "descriptorSetLayout";
	}
	~DescriptorSetLayoutMap()
	{
		for (auto& pair : resources)
			vkDestroyDescriptorSetLayout(device, pair.second, nullptr);
	}

	VkDescriptorSetLayout create(const StringId& name, const VkDescriptorSetLayoutCreateInfo& createInfo)
	{
		VkDescriptorSetLayout rsrc;
		VLKCHECK(vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &rsrc));
		resources[name] = rsrc;
		return rsrc;
	}
	VkDescriptorSetLayout create(const char* name, const VkDescriptorSetLayoutCreateInfo& createInfo)
	{
		return create(sid(name), createInfo);
	}
};

class PipelineMap final : public ResourceMap<VkPipeline> {
public:
	explicit PipelineMap(VkDevice device)
		: ResourceMap{ device }
	{
		resourceType = "pipeline";
	}
	~PipelineMap()
	{
		for (auto& pair : resources)
			vkDestroyPipeline(device, pair.second, nullptr);
	}

	VkPipeline create(const StringId& name, const VkGraphicsPipelineCreateInfo& createInfo)
	{
		VkPipeline pipeline;
		VLKCHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline));
		resources[name] = pipeline;
		return pipeline;
	}
	VkPipeline create(const char* name, const VkGraphicsPipelineCreateInfo& createInfo)
	{
		return create(sid(name), createInfo);
	}
};

class DescriptorSetMap final : public ResourceMap<VkDescriptorSet> {
public:
	explicit DescriptorSetMap(VkDevice device)
		: ResourceMap{ device }
	{
		resourceType = "descriptorSet";
	}

	~DescriptorSetMap() {}

	VkDescriptorSet create(const StringId& name, const VkDescriptorSetAllocateInfo& allocInfo)
	{
		VkDescriptorSet descriptorSet;
		VLKCHECK(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
		logging::info(
			__FILE__, ":", __LINE__, ": Allocated ", allocInfo.descriptorSetCount, " descriptor sets");

		resources[name] = descriptorSet;
		return descriptorSet;
	}

	VkDescriptorSet create(const char* name, const VkDescriptorSetAllocateInfo& allocInfo)
	{
		return create(sid(name), allocInfo);
	}
};

class SemaphoreMap final : public ResourceMap<VkSemaphore> {
public:
	explicit SemaphoreMap(VkDevice device)
		: ResourceMap{ device }
	{
		resourceType = "semaphore";
	}
	~SemaphoreMap()
	{
		for (auto& pair : resources)
			vkDestroySemaphore(device, pair.second, nullptr);
	}

	VkSemaphore create(const StringId& name)
	{
		VkSemaphoreCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkSemaphore semaphore;
		VLKCHECK(vkCreateSemaphore(device, &createInfo, nullptr, &semaphore));
		resources[name] = semaphore;
		return semaphore;
	}
	VkSemaphore create(const char* name) { return create(sid(name)); }
};

struct Resources {
	PipelineLayoutMap* pipelineLayouts;
	PipelineMap* pipelines;
	DescriptorSetLayoutMap* descriptorSetLayouts;
	DescriptorSetMap* descriptorSets;
	SemaphoreMap* semaphores;

	void init(VkDevice device)
	{
		constexpr auto s0 = sizeof(PipelineLayoutMap);
		constexpr auto s1 = sizeof(PipelineMap);
		constexpr auto s2 = sizeof(DescriptorSetLayoutMap);
		constexpr auto s3 = sizeof(DescriptorSetMap);
		constexpr auto s4 = sizeof(SemaphoreMap);

		// Use a single allocation for the backing memory
		mem = new uint8_t[s0 + s1 + s2 + s3 + s4];
		pipelineLayouts = new (mem) PipelineLayoutMap(device);
		pipelines = new (mem + s0) PipelineMap(device);
		descriptorSetLayouts = new (mem + s0 + s1) DescriptorSetLayoutMap(device);
		descriptorSets = new (mem + s0 + s1 + s2) DescriptorSetMap(device);
		semaphores = new (mem + s0 + s1 + s2 + s3) SemaphoreMap(device);
	}

	void cleanup()
	{
		semaphores->~SemaphoreMap();
		descriptorSets->~DescriptorSetMap();
		descriptorSetLayouts->~DescriptorSetLayoutMap();
		pipelines->~PipelineMap();
		pipelineLayouts->~PipelineLayoutMap();
		delete[] mem;
	}

private:
	uint8_t* mem;
};
