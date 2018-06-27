#include "skybox.hpp"
#include "application.hpp"
#include "logging.hpp"
#include "textures.hpp"
#include "vulk_errors.hpp"
#include <array>

using namespace logging;

Image createSkybox(const Application& app)
{
	return createTextureCube(app,
		{
			"textures/skybox/devils_advocate_rt.tga",
			"textures/skybox/devils_advocate_lf.tga",
			"textures/skybox/devils_advocate_up.tga",
			"textures/skybox/devils_advocate_dn.tga",
			"textures/skybox/devils_advocate_ft.tga",
			"textures/skybox/devils_advocate_bk.tga",
		});
}

std::vector<VkDescriptorSetLayout> createSkyboxDescriptorSetLayouts(const Application& app)
{
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	descriptorSetLayouts.reserve(1);

	{
		//// Set #0: view resources

		// ViewUbo
		VkDescriptorSetLayoutBinding viewUboBinding = {};
		viewUboBinding.binding = 0;
		viewUboBinding.descriptorCount = 1;
		viewUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		viewUboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		// Skybox image
		VkDescriptorSetLayoutBinding skyboxCubeBinding = {};
		skyboxCubeBinding.binding = 1;
		skyboxCubeBinding.descriptorCount = 1;
		skyboxCubeBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		skyboxCubeBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		const std::array<VkDescriptorSetLayoutBinding, 2> bindings = { viewUboBinding, skyboxCubeBinding };

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = bindings.size();
		layoutInfo.pBindings = bindings.data();

		VkDescriptorSetLayout descriptorSetLayout;
		VLKCHECK(vkCreateDescriptorSetLayout(app.device, &layoutInfo, nullptr, &descriptorSetLayout));
		app.validation.addObjectInfo(descriptorSetLayout, __FILE__, __LINE__);

		descriptorSetLayouts.emplace_back(descriptorSetLayout);
	}

	return descriptorSetLayouts;
}

std::vector<VkDescriptorSet> createSkyboxDescriptorSets(const Application& app,
	const CombinedUniformBuffers& uniformBuffers,
	VkSampler cubeSampler)
{
	std::vector<VkDescriptorSetLayout> layouts;
	layouts.reserve(1);
	layouts.emplace_back(app.res.descriptorSetLayouts->get("skybox"));

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = app.descriptorPool;
	allocInfo.descriptorSetCount = layouts.size();
	allocInfo.pSetLayouts = layouts.data();

	debug(__FILE__, ":", __LINE__, ": Allocating ", allocInfo.descriptorSetCount, " descriptor sets");
	std::vector<VkDescriptorSet> descriptorSets(layouts.size());
	VLKCHECK(vkAllocateDescriptorSets(app.device, &allocInfo, descriptorSets.data()));
	for (const auto& descriptorSet : descriptorSets)
		app.validation.addObjectInfo(descriptorSet, __FILE__, __LINE__);

	// Update descriptor sets

	std::vector<VkWriteDescriptorSet> descriptorWrites;

	//// Set #0: view resources
	VkDescriptorBufferInfo viewUboInfo = {};
	viewUboInfo.buffer = uniformBuffers.handle;
	viewUboInfo.offset = uniformBuffers.offsets.perView;
	viewUboInfo.range = sizeof(ViewUniformBufferObject);
	{
		VkWriteDescriptorSet descriptorWrite = {};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSets[0];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &viewUboInfo;

		descriptorWrites.emplace_back(descriptorWrite);
	}

	VkDescriptorImageInfo skyboxInfo = {};
	skyboxInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	skyboxInfo.imageView = app.skybox.image.view;
	skyboxInfo.sampler = cubeSampler;
	{
		VkWriteDescriptorSet descriptorWrite = {};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSets[0];
		descriptorWrite.dstBinding = 1;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pImageInfo = &skyboxInfo;

		descriptorWrites.emplace_back(descriptorWrite);
	}

	vkUpdateDescriptorSets(app.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	return descriptorSets;
}
