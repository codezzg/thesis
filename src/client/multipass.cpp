#include "multipass.hpp"
#include "buffers.hpp"
#include "images.hpp"
#include "application.hpp"
#include <array>

void recordMultipassCommandBuffers(const Application& app, std::vector<VkCommandBuffer>& commandBuffers,
		uint32_t nIndices, const Buffer& vBuffer, const Buffer& iBuffer)
{
	std::array<VkClearValue, 5> clearValues = {};
	clearValues[0].color = { 0.f, 0.0f, 0.f, 1.f };
	clearValues[1].depthStencil = { 1, 0 };

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = app.renderPass;
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = app.swapChain.extent;
	renderPassInfo.clearValueCount = clearValues.size();
	renderPassInfo.pClearValues = clearValues.data();

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

	for (size_t i = 0; i < commandBuffers.size(); ++i) {
		VLKCHECK(vkBeginCommandBuffer(commandBuffers[i], &beginInfo));

		renderPassInfo.framebuffer = app.swapChain.framebuffers[i];

		//// First subpass
		vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
				app.res.pipelineLayouts->get("multi"), 0, 1,
				&app.res.descriptorSets->get("multi"), 0, nullptr);
		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, app.gBuffer.pipeline);

		std::array<VkBuffer, 1> vertexBuffers = { vBuffer.handle };
		const std::array<VkDeviceSize, 1> offsets = { 0 };
		vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers.data(), offsets.data());
		vkCmdBindIndexBuffer(commandBuffers[i], iBuffer.handle, 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(commandBuffers[i], nIndices, 1, 0, 0, 0);

		//// Second subpass
		vkCmdNextSubpass(commandBuffers[i], VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, app.swapChain.pipeline);

		vertexBuffers[0] = app.screenQuadBuffer.handle;
		vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers.data(), offsets.data());
		vkCmdDraw(commandBuffers[i], 4, 1, 0, 0);

		vkCmdEndRenderPass(commandBuffers[i]);

		VLKCHECK(vkEndCommandBuffer(commandBuffers[i]));
	}
}

VkDescriptorSetLayout createMultipassDescriptorSetLayout(const Application& app) {
	// TexDiffuse
	VkDescriptorSetLayoutBinding diffuseLayoutBinding = {};
	diffuseLayoutBinding.binding = 0;
	diffuseLayoutBinding.descriptorCount = 1;
	diffuseLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	diffuseLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// TexSpecular
	VkDescriptorSetLayoutBinding specLayoutBinding = {};
	specLayoutBinding.binding = 1;
	specLayoutBinding.descriptorCount = 1;
	specLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	specLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// ubo: MVPUniformBufferObject
	VkDescriptorSetLayoutBinding mvpUboLayoutBinding = {};
	mvpUboLayoutBinding.binding = 2;
	mvpUboLayoutBinding.descriptorCount = 1;
	mvpUboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	mvpUboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;


	// gPosition: sampler2D
	VkDescriptorSetLayoutBinding gPosLayoutBinding = {};
	gPosLayoutBinding.binding = 3;
	gPosLayoutBinding.descriptorCount = 1;
	gPosLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	gPosLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// gNormal: sampler2D
	VkDescriptorSetLayoutBinding gNormalLayoutBinding = {};
	gNormalLayoutBinding.binding = 4;
	gNormalLayoutBinding.descriptorCount = 1;
	gNormalLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	gNormalLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// gAlbedoSpec: sampler2D
	VkDescriptorSetLayoutBinding gAlbedoSpecLayoutBinding = {};
	gAlbedoSpecLayoutBinding.binding = 5;
	gAlbedoSpecLayoutBinding.descriptorCount = 1;
	gAlbedoSpecLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	gAlbedoSpecLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// ubo: CompositionUniformBufferObject
	VkDescriptorSetLayoutBinding compUboLayoutBinding = {};
	compUboLayoutBinding.binding = 6;
	compUboLayoutBinding.descriptorCount = 1;
	compUboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	compUboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	const std::array<VkDescriptorSetLayoutBinding, 7> bindings = {
		diffuseLayoutBinding,
		specLayoutBinding,
		mvpUboLayoutBinding,
		gPosLayoutBinding,
		gNormalLayoutBinding,
		gAlbedoSpecLayoutBinding,
		compUboLayoutBinding,
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = bindings.size();
	layoutInfo.pBindings = bindings.data();

	VkDescriptorSetLayout descriptorSetLayout;
	VLKCHECK(vkCreateDescriptorSetLayout(app.device, &layoutInfo, nullptr, &descriptorSetLayout));
	app.validation.addObjectInfo(descriptorSetLayout, __FILE__, __LINE__);

	return descriptorSetLayout;
}

VkDescriptorSet createMultipassDescriptorSet(const Application& app,
		const CombinedUniformBuffers& uniformBuffers,
		const Image& texDiffuse, const Image& texSpecular, VkSampler texSampler)
{
	const std::array<VkDescriptorSetLayout, 1> layouts = { app.res.descriptorSetLayouts->get("multi") };
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = app.descriptorPool;
	allocInfo.descriptorSetCount = layouts.size();
	allocInfo.pSetLayouts = layouts.data();

	VkDescriptorSet descriptorSet;
	VLKCHECK(vkAllocateDescriptorSets(app.device, &allocInfo, &descriptorSet));
	app.validation.addObjectInfo(descriptorSet, __FILE__, __LINE__);


	VkDescriptorImageInfo diffuseInfo = {};
	diffuseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	diffuseInfo.imageView = texDiffuse.view;
	diffuseInfo.sampler = texSampler;

	VkDescriptorImageInfo specInfo = {};
	specInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	specInfo.imageView = texSpecular.view;
	specInfo.sampler = texSampler;

	VkDescriptorImageInfo gPositionInfo = {};
	gPositionInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	gPositionInfo.imageView = app.gBuffer.position.view;
	gPositionInfo.sampler = texSampler;

	VkDescriptorImageInfo gNormalInfo = {};
	gNormalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	gNormalInfo.imageView = app.gBuffer.normal.view;
	gNormalInfo.sampler = texSampler;

	VkDescriptorImageInfo gAlbedoSpecInfo = {};
	gAlbedoSpecInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	gAlbedoSpecInfo.imageView = app.gBuffer.albedoSpec.view;
	gAlbedoSpecInfo.sampler = texSampler;

	VkDescriptorBufferInfo mvpUboInfo = {};
	mvpUboInfo.buffer = uniformBuffers.handle;
	mvpUboInfo.offset = uniformBuffers.offsets.mvp;
	mvpUboInfo.range = sizeof(MVPUniformBufferObject);

	VkDescriptorBufferInfo compUboInfo = {};
	compUboInfo.buffer = uniformBuffers.handle;
	compUboInfo.offset = uniformBuffers.offsets.comp;
	compUboInfo.range = sizeof(CompositionUniformBufferObject);

	std::array<VkWriteDescriptorSet, 7> descriptorWrites = {};

	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pImageInfo = &diffuseInfo;

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = descriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].pImageInfo = &specInfo;

	descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[2].dstSet = descriptorSet;
	descriptorWrites[2].dstBinding = 2;
	descriptorWrites[2].dstArrayElement = 0;
	descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[2].descriptorCount = 1;
	descriptorWrites[2].pBufferInfo = &mvpUboInfo;

	descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[3].dstSet = descriptorSet;
	descriptorWrites[3].dstBinding = 3;
	descriptorWrites[3].dstArrayElement = 0;
	descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	descriptorWrites[3].descriptorCount = 1;
	descriptorWrites[3].pImageInfo = &gPositionInfo;

	descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[4].dstSet = descriptorSet;
	descriptorWrites[4].dstBinding = 4;
	descriptorWrites[4].dstArrayElement = 0;
	descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	descriptorWrites[4].descriptorCount = 1;
	descriptorWrites[4].pImageInfo = &gNormalInfo;

	descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[5].dstSet = descriptorSet;
	descriptorWrites[5].dstBinding = 5;
	descriptorWrites[5].dstArrayElement = 0;
	descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	descriptorWrites[5].descriptorCount = 1;
	descriptorWrites[5].pImageInfo = &gAlbedoSpecInfo;

	descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[6].dstSet = descriptorSet;
	descriptorWrites[6].dstBinding = 6;
	descriptorWrites[6].dstArrayElement = 0;
	descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[6].descriptorCount = 1;
	descriptorWrites[6].pBufferInfo = &compUboInfo;

	vkUpdateDescriptorSets(app.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	return descriptorSet;
}
