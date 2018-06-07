#include "multipass.hpp"
#include "application.hpp"
#include "buffers.hpp"
#include "client_resources.hpp"
#include "logging.hpp"
#include "materials.hpp"
#include "utils.hpp"
#include <array>

using namespace logging;

void recordMultipassCommandBuffers(const Application& app,
	std::vector<VkCommandBuffer>& commandBuffers,
	uint32_t nIndices,
	const Buffer& vBuffer,
	const Buffer& iBuffer,
	const NetworkResources& netRsrc)
{
	std::array<VkClearValue, 5> clearValues = {};
	clearValues[0].color = { 0.f, 0.2f, 0.6f, 1.f };
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

		// Bind view resources
		vkCmdBindDescriptorSets(commandBuffers[i],
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			app.res.pipelineLayouts->get("multi"),
			0,
			1,
			&app.res.descriptorSets->get("view_res"),
			0,
			nullptr);

		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, app.gBuffer.pipeline);
		// Bind shader resources
		vkCmdBindDescriptorSets(commandBuffers[i],
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			app.res.pipelineLayouts->get("multi"),
			1,
			1,
			&app.res.descriptorSets->get("shader_res"),
			0,
			nullptr);

		// TODO: reorganize material / meshes hierarchy so that materials are higher
		std::array<VkBuffer, 1> vertexBuffers = { vBuffer.handle };
		const std::array<VkDeviceSize, 1> offsets = { 0 };
		// Draw all meshes (i.e. for now, all materials)
		vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers.data(), offsets.data());
		vkCmdBindIndexBuffer(commandBuffers[i], iBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
		for (const auto& modelpair : netRsrc.models) {
			const auto& model = modelpair.second;
			for (const auto& mesh : model.meshes) {
				const auto& matName =
					mesh.materialId >= 0 ? model.materials[mesh.materialId] : SID_NONE;

				vkCmdBindDescriptorSets(commandBuffers[i],
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					app.res.pipelineLayouts->get("multi"),
					2,
					1,
					&app.res.descriptorSets->get(matName),
					0,
					nullptr);
				vkCmdBindDescriptorSets(commandBuffers[i],
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					app.res.pipelineLayouts->get("multi"),
					3,
					1,
					&app.res.descriptorSets->get("obj_res"),
					0,
					nullptr);

				vkCmdDrawIndexed(commandBuffers[i], mesh.len, 1, mesh.offset, 0, 0);
			}
		}

		//// Second subpass
		vkCmdNextSubpass(commandBuffers[i], VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, app.swapChain.pipeline);

		vertexBuffers[0] = app.screenQuadBuffer.handle;
		vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers.data(), offsets.data());
		// vkCmdBindDescriptorSets(commandBuffers[i], app.res.pipelineLayouts->get("swap"),
		// 0, 1,
		vkCmdDraw(commandBuffers[i], 4, 1, 0, 0);

		vkCmdEndRenderPass(commandBuffers[i]);

		VLKCHECK(vkEndCommandBuffer(commandBuffers[i]));
	}
}

std::vector<VkDescriptorSetLayout> createMultipassDescriptorSetLayouts(const Application& app)
{
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	descriptorSetLayouts.reserve(4);

	{
		//// Set #0: view resources

		// CompUbo
		VkDescriptorSetLayoutBinding compUboBinding = {};
		compUboBinding.binding = 0;
		compUboBinding.descriptorCount = 1;
		compUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		compUboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		const std::array<VkDescriptorSetLayoutBinding, 1> bindings = { compUboBinding };

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = bindings.size();
		layoutInfo.pBindings = bindings.data();

		VkDescriptorSetLayout descriptorSetLayout;
		VLKCHECK(vkCreateDescriptorSetLayout(app.device, &layoutInfo, nullptr, &descriptorSetLayout));
		app.validation.addObjectInfo(descriptorSetLayout, __FILE__, __LINE__);

		descriptorSetLayouts.emplace_back(descriptorSetLayout);
	}

	{
		//// Set #1: shader resources

		// gPosition: sampler2D
		VkDescriptorSetLayoutBinding gPosLayoutBinding = {};
		gPosLayoutBinding.binding = 0;
		gPosLayoutBinding.descriptorCount = 1;
		gPosLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		gPosLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		// gNormal: sampler2D
		VkDescriptorSetLayoutBinding gNormalLayoutBinding = {};
		gNormalLayoutBinding.binding = 1;
		gNormalLayoutBinding.descriptorCount = 1;
		gNormalLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		gNormalLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		// gAlbedoSpec: sampler2D
		VkDescriptorSetLayoutBinding gAlbedoSpecLayoutBinding = {};
		gAlbedoSpecLayoutBinding.binding = 2;
		gAlbedoSpecLayoutBinding.descriptorCount = 1;
		gAlbedoSpecLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		gAlbedoSpecLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		const std::array<VkDescriptorSetLayoutBinding, 3> bindings = {
			gPosLayoutBinding, gNormalLayoutBinding, gAlbedoSpecLayoutBinding
		};

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = bindings.size();
		layoutInfo.pBindings = bindings.data();

		VkDescriptorSetLayout descriptorSetLayout;
		VLKCHECK(vkCreateDescriptorSetLayout(app.device, &layoutInfo, nullptr, &descriptorSetLayout));
		app.validation.addObjectInfo(descriptorSetLayout, __FILE__, __LINE__);

		descriptorSetLayouts.emplace_back(descriptorSetLayout);
	}

	{
		//// Set #2: material resources

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

		// TexNormal
		VkDescriptorSetLayoutBinding normLayoutBinding = {};
		normLayoutBinding.binding = 2;
		normLayoutBinding.descriptorCount = 1;
		normLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		normLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		const std::array<VkDescriptorSetLayoutBinding, 3> bindings = {
			diffuseLayoutBinding, specLayoutBinding, normLayoutBinding
		};

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = bindings.size();
		layoutInfo.pBindings = bindings.data();

		VkDescriptorSetLayout descriptorSetLayout;
		VLKCHECK(vkCreateDescriptorSetLayout(app.device, &layoutInfo, nullptr, &descriptorSetLayout));
		app.validation.addObjectInfo(descriptorSetLayout, __FILE__, __LINE__);

		descriptorSetLayouts.emplace_back(descriptorSetLayout);
	}

	{
		//// Set #3: object resources

		// MVP
		VkDescriptorSetLayoutBinding mvpUboLayoutBinding = {};
		mvpUboLayoutBinding.binding = 0;
		mvpUboLayoutBinding.descriptorCount = 1;
		mvpUboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		mvpUboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		const std::array<VkDescriptorSetLayoutBinding, 1> bindings = { mvpUboLayoutBinding };

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

std::vector<VkDescriptorSet> createMultipassDescriptorSets(const Application& app,
	const CombinedUniformBuffers& uniformBuffers,
	const std::vector<Material>& materials,
	VkSampler texSampler)
{
	std::vector<VkDescriptorSetLayout> layouts(1 + 1 + materials.size() + 1 /*TODO: n.objects*/);
	layouts[0] = app.res.descriptorSetLayouts->get("view_res");     // we only have 1 view
	layouts[1] = app.res.descriptorSetLayouts->get("shader_res");   // we only have 1 pipeline w/resources
	for (unsigned i = 1; i < materials.size() + 1; ++i)
		layouts[1 + i] = app.res.descriptorSetLayouts->get("mat_res");
	layouts[materials.size() + 2] = app.res.descriptorSetLayouts->get("obj_res");   // we only have 1 model

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

	std::vector<VkWriteDescriptorSet> descriptorWrites;
	descriptorWrites.reserve(descriptorSets.size());

	//// Set #0: view resources
	VkDescriptorBufferInfo compUboInfo = {};
	compUboInfo.buffer = uniformBuffers.handle;
	compUboInfo.offset = uniformBuffers.offsets.comp;
	compUboInfo.range = sizeof(CompositionUniformBufferObject);
	{
		VkWriteDescriptorSet descriptorWrite = {};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSets[0];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &compUboInfo;

		descriptorWrites.emplace_back(descriptorWrite);
	}

	//// Set #1: shader resources
	VkDescriptorImageInfo gPositionInfo = {};
	gPositionInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	gPositionInfo.imageView = app.gBuffer.position.view;
	gPositionInfo.sampler = texSampler;
	{
		VkWriteDescriptorSet descriptorWrite = {};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSets[1];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pImageInfo = &gPositionInfo;

		descriptorWrites.emplace_back(descriptorWrite);
	}

	VkDescriptorImageInfo gNormalInfo = {};
	gNormalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	gNormalInfo.imageView = app.gBuffer.normal.view;
	gNormalInfo.sampler = texSampler;
	{
		VkWriteDescriptorSet descriptorWrite = {};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSets[1];
		descriptorWrite.dstBinding = 1;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pImageInfo = &gNormalInfo;

		descriptorWrites.emplace_back(descriptorWrite);
	}

	VkDescriptorImageInfo gAlbedoSpecInfo = {};
	gAlbedoSpecInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	gAlbedoSpecInfo.imageView = app.gBuffer.albedoSpec.view;
	gAlbedoSpecInfo.sampler = texSampler;
	{
		VkWriteDescriptorSet descriptorWrite = {};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSets[1];
		descriptorWrite.dstBinding = 2;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pImageInfo = &gAlbedoSpecInfo;

		descriptorWrites.emplace_back(descriptorWrite);
	}

	//// Set #2: material resources
	std::vector<VkDescriptorImageInfo> diffuseInfos(materials.size());
	std::vector<VkDescriptorImageInfo> specularInfos(materials.size());
	std::vector<VkDescriptorImageInfo> normalInfos(materials.size());

	for (unsigned i = 0; i < materials.size(); ++i) {
		diffuseInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		diffuseInfos[i].sampler = texSampler;
		diffuseInfos[i].imageView = materials[i].diffuse;
		{
			VkWriteDescriptorSet descriptorWrite = {};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = descriptorSets[2 + i];
			descriptorWrite.dstBinding = 0;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &diffuseInfos[i];

			descriptorWrites.emplace_back(descriptorWrite);
		}

		specularInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		specularInfos[i].sampler = texSampler;
		specularInfos[i].imageView = materials[i].specular;
		{
			VkWriteDescriptorSet descriptorWrite = {};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = descriptorSets[2 + i];
			descriptorWrite.dstBinding = 1;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &specularInfos[i];

			descriptorWrites.emplace_back(descriptorWrite);
		}

		normalInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		normalInfos[i].sampler = texSampler;
		normalInfos[i].imageView = materials[i].normal;
		{
			VkWriteDescriptorSet descriptorWrite = {};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = descriptorSets[2 + i];
			descriptorWrite.dstBinding = 2;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &normalInfos[i];

			descriptorWrites.emplace_back(descriptorWrite);
		}
	}

	//// Set #3: object resources
	VkDescriptorBufferInfo mvpUboInfo = {};
	mvpUboInfo.buffer = uniformBuffers.handle;
	mvpUboInfo.offset = uniformBuffers.offsets.mvp;
	mvpUboInfo.range = sizeof(MVPUniformBufferObject);
	{
		VkWriteDescriptorSet descriptorWrite = {};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSets[2 + materials.size()];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &mvpUboInfo;

		descriptorWrites.emplace_back(descriptorWrite);
	}

	vkUpdateDescriptorSets(app.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	return descriptorSets;
}
