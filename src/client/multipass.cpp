#include "multipass.hpp"
#include "application.hpp"
#include "buffer_array.hpp"
#include "buffers.hpp"
#include "client_resources.hpp"
#include "geometry.hpp"
#include "logging.hpp"
#include "materials.hpp"
#include "utils.hpp"
#include "vertex.hpp"
#include <array>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

using namespace logging;

inline static void recordDrawModels(const Application& app,
	VkCommandBuffer cmdBuf,
	const Geometry& geometry,
	const NetworkResources& netRsrc,
	const BufferArray& uniformBuffers)
{
	assert(geometry.locations.size() == netRsrc.models.size() &&
		"Geometry locations should be the same number as models!");

	std::array<VkBuffer, 1> vertexBuffers = { geometry.vertexBuffer.handle };
	std::vector<VkDeviceSize> offsets(geometry.locations.size());

	// Use the same vertex buffer for all models, but with a different offset for each model.
	// The offsets to use are saved in geometry.locations.
	for (unsigned j = 0; j < netRsrc.models.size(); ++j) {
		auto loc_it = geometry.locations.find(netRsrc.models[j].name);
		assert(loc_it != geometry.locations.end());
		offsets[j] = loc_it->second.vertexOff;
	}

	for (unsigned j = 0; j < netRsrc.models.size(); ++j) {
		const auto& model = netRsrc.models[j];

		// Bind the vertex buffer at the proper offset
		vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers.data(), &offsets[j]);

		// Bind the index buffer at the proper offset
		const auto loc_it = geometry.locations.find(model.name);
		assert(loc_it != geometry.locations.end());
		vkCmdBindIndexBuffer(
			cmdBuf, geometry.indexBuffer.handle, loc_it->second.indexOff, VK_INDEX_TYPE_UINT32);

		const auto ubo = uniformBuffers.getBuffer(model.name);
		assert(ubo);

		const auto dynOff = static_cast<uint32_t>(ubo->bufOffset);
		// Bind object descriptor set
		vkCmdBindDescriptorSets(cmdBuf,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			app.res.pipelineLayouts->get("multi"),
			3,
			1,
			&app.res.descriptorSets->get(model.name),
			1,
			&dynOff);

		for (const auto& mesh : model.meshes) {
			const auto& matName = mesh.materialId >= 0 ? model.materials[mesh.materialId] : SID_NONE;

			// Bind material descriptor set
			vkCmdBindDescriptorSets(cmdBuf,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				app.res.pipelineLayouts->get("multi"),
				2,
				1,
				&app.res.descriptorSets->get(matName),
				0,
				nullptr);

			vkCmdDrawIndexed(cmdBuf, mesh.len, 1, mesh.offset, 0, 0);
		}
	}
}

void recordMultipassCommandBuffers(const Application& app,
	std::vector<VkCommandBuffer>& commandBuffers,
	const Geometry& geometry,
	const NetworkResources& netRsrc,
	const BufferArray& uniformBuffers)
{
	std::array<VkClearValue, 5> clearValues = {};
	constexpr auto fm = std::numeric_limits<float>::max();
	clearValues[0].color = { 0, 0, 0, 0 };
	clearValues[1].depthStencil = { 1, 0 };
	// Pos
	clearValues[2].color = { fm, fm, fm, fm };
	// Norm
	clearValues[3].color = { 0.f, 0.f, 0.f, 0.f };
	// "Sky" color (TODO replace with skybox)
	// clearValues[4].color = { 0.41f, 0.84f, 0.87f, 0.f };
	if (netRsrc.models.size() == 0)
		clearValues[4].color = { 0.0f, 0.0f, 0.0f, 0.f };
	else
		clearValues[4].color = { 0.2f, 0.2f, 0.2f, 0.f };

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

	VkViewport viewport = {};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = static_cast<float>(app.swapChain.extent.width);
	viewport.height = static_cast<float>(app.swapChain.extent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = app.swapChain.extent;

	for (size_t i = 0; i < commandBuffers.size(); ++i) {
		auto& cmdBuf = commandBuffers[i];

		VLKCHECK(vkBeginCommandBuffer(cmdBuf, &beginInfo));

		renderPassInfo.framebuffer = app.swapChain.framebuffers[i];

		//// First subpass: fill gbuffer
		vkCmdBeginRenderPass(cmdBuf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Set dynamic state
		vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
		vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

		// Bind view resources
		vkCmdBindDescriptorSets(cmdBuf,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			app.res.pipelineLayouts->get("multi"),
			0,
			1,
			&app.res.descriptorSets->get("view_res"),
			0,
			nullptr);

		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, app.res.pipelines->get("gbuffer"));
		// Bind shader resources
		vkCmdBindDescriptorSets(cmdBuf,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			app.res.pipelineLayouts->get("multi"),
			1,
			1,
			&app.res.descriptorSets->get("gbuffer_res"),
			0,
			nullptr);

		// TODO: reorganize material / meshes hierarchy so that materials are higher
		// (i.e. group all meshes using the same material and draw them with the same
		// material descriptor set)

		// Draw all models
		if (netRsrc.models.size() > 0)
			recordDrawModels(app, cmdBuf, geometry, netRsrc, uniformBuffers);

		//// Second subpass: draw skybox
		vkCmdNextSubpass(cmdBuf, VK_SUBPASS_CONTENTS_INLINE);

		// TODO make skybox work
		/*
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, app.skybox.pipeline);
		vertexBuffers[0] = app.skybox.buffer.handle;
		vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers.data(), offsets.data());

		// Retreive first index location inside the skybox's buffer.
		// Since it stores [vertices|indices] and indexOff is given in bytes, we pretend that
		// the previous vertices are unused indices (this is possible since sizeof(Vertex) is
		// multiple of sizeof(uint16_t)) and use that as the offset.
		static_assert(
			sizeof(Vertex) % sizeof(uint16_t) == 0, "sizeof(Vertex) is not multiple of sizeof(uint16_t)!");
		const auto skyFirstIndex = app.skybox.indexOff / sizeof(uint16_t);
		const auto skyNIndices = (app.skybox.buffer.size - app.skybox.indexOff) / sizeof(uint16_t);
		vkCmdBindIndexBuffer(cmdBuf, app.skybox.buffer.handle, skyFirstIndex, VK_INDEX_TYPE_UINT16);

		// vkCmdDrawIndexed(cmdBuf, skyNIndices, 1, 0, 0, 0);*/

		//// Third subpass: draw combined gbuffer images into a fullscreen quad
		vkCmdNextSubpass(cmdBuf, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, app.res.pipelines->get("swap"));

		const std::array<VkDeviceSize, 1> offsets = { 0 };
		vkCmdBindVertexBuffers(cmdBuf, 0, 1, &app.screenQuadBuffer.handle, offsets.data());
		vkCmdDraw(cmdBuf, 4, 1, 0, 0);

		vkCmdEndRenderPass(cmdBuf);

		VLKCHECK(vkEndCommandBuffer(cmdBuf));
	}
}

// Rationale why I'm using different descriptor sets rather than just one:
// https://developer.nvidia.com/vulkan-shader-resource-binding
std::vector<VkDescriptorSetLayout> createMultipassDescriptorSetLayouts(const Application& app)
{
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	descriptorSetLayouts.reserve(4);

	{
		//// Set #0: view resources

		// ViewUbo
		VkDescriptorSetLayoutBinding viewUboBinding = {};
		viewUboBinding.binding = 0;
		viewUboBinding.descriptorCount = 1;
		viewUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		viewUboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		// Skybox
		VkDescriptorSetLayoutBinding skyboxLayoutBinding = {};
		skyboxLayoutBinding.binding = 1;
		skyboxLayoutBinding.descriptorCount = 1;
		skyboxLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		skyboxLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		const std::array<VkDescriptorSetLayoutBinding, 2> bindings = { viewUboBinding, skyboxLayoutBinding };

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
		//// Set #1: gbuffer resources

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

		// ObjectUbo
		VkDescriptorSetLayoutBinding objectUboLayoutBinding = {};
		objectUboLayoutBinding.binding = 0;
		objectUboLayoutBinding.descriptorCount = 1;
		objectUboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		objectUboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		const std::array<VkDescriptorSetLayoutBinding, 1> bindings = { objectUboLayoutBinding };

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = bindings.size();
		layoutInfo.pBindings = bindings.data();

		VkDescriptorSetLayout descriptorSetLayout;
		VLKCHECK(vkCreateDescriptorSetLayout(app.device, &layoutInfo, nullptr, &descriptorSetLayout));
		app.validation.addObjectInfo(descriptorSetLayout, __FILE__, __LINE__);

		descriptorSetLayouts.emplace_back(descriptorSetLayout);
	}

	assert(descriptorSetLayouts.size() == 4);

	return descriptorSetLayouts;
}

std::vector<VkDescriptorSet> createMultipassPermanentDescriptorSets(const Application& app,
	const BufferArray& uniformBuffers,
	VkSampler texSampler)
{
	std::vector<VkDescriptorSetLayout> layouts(2);

	// 1 descriptor set per view
	layouts[0] = app.res.descriptorSetLayouts->get("view_res");

	// 1 descriptor set for the gbuffer
	layouts[1] = app.res.descriptorSetLayouts->get("gbuffer_res");

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
	descriptorWrites.reserve(layouts.size());

	//// Set #0: view resources
	{
		auto viewBuf = uniformBuffers.getBuffer(sid("view"));
		assert(viewBuf);

		VkDescriptorBufferInfo viewUboInfo = {};
		viewUboInfo.buffer = viewBuf->handle;
		viewUboInfo.offset = viewBuf->bufOffset;
		viewUboInfo.range = viewBuf->size;

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

	//// Set #1: gbuffer shader resources
	{
		VkDescriptorImageInfo gPositionInfo = {};
		gPositionInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		gPositionInfo.imageView = app.gBuffer.position.view;
		gPositionInfo.sampler = texSampler;

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

	{
		VkDescriptorImageInfo gNormalInfo = {};
		gNormalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		gNormalInfo.imageView = app.gBuffer.normal.view;
		gNormalInfo.sampler = texSampler;

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

	{
		VkDescriptorImageInfo gAlbedoSpecInfo = {};
		gAlbedoSpecInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		gAlbedoSpecInfo.imageView = app.gBuffer.albedoSpec.view;
		gAlbedoSpecInfo.sampler = texSampler;

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

	vkUpdateDescriptorSets(app.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	return descriptorSets;
}

std::vector<VkDescriptorSet> createMultipassTransitoryDescriptorSets(const Application& app,
	const BufferArray& uniformBuffers,
	const std::vector<Material>& materials,
	const std::vector<ModelInfo>& models,
	VkSampler texSampler,
	VkSampler cubeSampler)
{
	std::vector<VkDescriptorSetLayout> layouts(materials.size() + models.size());

	// 1 descriptor set per material
	for (unsigned i = 0; i < materials.size(); ++i)
		layouts[i] = app.res.descriptorSetLayouts->get("mat_res");

	// 1 descriptor set per model
	// NOTE: technically we may only have 1 descriptor set per backing buffer inside the
	// `uniformBuffers` BufferArray; however, for simplicity we just use 1 per model for now.
	for (unsigned i = 0; i < models.size(); ++i)
		layouts[materials.size() + i] = app.res.descriptorSetLayouts->get("obj_res");

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
	descriptorWrites.reserve(layouts.size());

	// Skybox
	/*{
		VkDescriptorImageInfo skyboxInfo = {};
		skyboxInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		skyboxInfo.imageView = app.skybox.image.view;
		skyboxInfo.sampler = cubeSampler;

		VkWriteDescriptorSet descriptorWrite = {};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSets[0];
		descriptorWrite.dstBinding = 1;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pImageInfo = &skyboxInfo;

		descriptorWrites.emplace_back(descriptorWrite);
	}*/

	//// Sets #0-#materials.size()+1: material resources
	std::vector<VkDescriptorImageInfo> diffuseInfos(materials.size());
	std::vector<VkDescriptorImageInfo> specularInfos(materials.size());
	std::vector<VkDescriptorImageInfo> normalInfos(materials.size());

	for (unsigned i = 0; i < materials.size(); ++i) {
		{
			diffuseInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			diffuseInfos[i].sampler = texSampler;
			diffuseInfos[i].imageView = materials[i].diffuse;

			VkWriteDescriptorSet descriptorWrite = {};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = descriptorSets[i];
			descriptorWrite.dstBinding = 0;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &diffuseInfos[i];

			descriptorWrites.emplace_back(descriptorWrite);
		}

		{
			specularInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			specularInfos[i].sampler = texSampler;
			specularInfos[i].imageView = materials[i].specular;

			VkWriteDescriptorSet descriptorWrite = {};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = descriptorSets[i];
			descriptorWrite.dstBinding = 1;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &specularInfos[i];

			descriptorWrites.emplace_back(descriptorWrite);
		}

		{
			normalInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			normalInfos[i].sampler = texSampler;
			normalInfos[i].imageView = materials[i].normal;

			VkWriteDescriptorSet descriptorWrite = {};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = descriptorSets[i];
			descriptorWrite.dstBinding = 2;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &normalInfos[i];

			descriptorWrites.emplace_back(descriptorWrite);
		}
	}

	//// Sets #materials.size() - ...: object resources
	for (unsigned i = 0; i < models.size(); ++i) {
		auto objBuf = uniformBuffers.getBuffer(models[i].name);
		assert(objBuf);

		VkDescriptorBufferInfo objUboInfo = {};
		objUboInfo.buffer = objBuf->handle;
		// The actual dynamic offset is specified while recording the command buffers
		objUboInfo.offset = 0;
		objUboInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet descriptorWrite = {};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSets[materials.size() + i];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &objUboInfo;

		descriptorWrites.emplace_back(descriptorWrite);
	}

	vkUpdateDescriptorSets(app.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	return descriptorSets;
}
