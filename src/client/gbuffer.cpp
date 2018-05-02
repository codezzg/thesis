#include "gbuffer.hpp"
#include "application.hpp"
#include "images.hpp"
#include "vulk_errors.hpp"
#include "textures.hpp"
#include "formats.hpp"
#include "shaders.hpp"
#include "vertex.hpp"
#include "commands.hpp"
#include <array>

static Image createPosAttachment(const Application& app) {
	auto positionImg = createImage(app,
		app.swapChain.extent.width, app.swapChain.extent.height,
		formats::position,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto positionImgView = createImageView(app, positionImg.handle,
			positionImg.format, VK_IMAGE_ASPECT_COLOR_BIT);

	positionImg.view = positionImgView;

	return positionImg;
}


static Image createNormalAttachment(const Application& app) {
	auto normalImg = createImage(app,
		app.swapChain.extent.width, app.swapChain.extent.height,
		formats::normal,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto normalImgView = createImageView(app, normalImg.handle,
			normalImg.format, VK_IMAGE_ASPECT_COLOR_BIT);

	normalImg.view = normalImgView;

	return normalImg;
}

static Image createAlbedoSpecAttachment(const Application& app) {
	auto albedoSpecImg = createImage(app,
		app.swapChain.extent.width, app.swapChain.extent.height,
		formats::albedoSpec,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto albedoSpecImgView = createImageView(app, albedoSpecImg.handle,
			albedoSpecImg.format,
			VK_IMAGE_ASPECT_COLOR_BIT);

	albedoSpecImg.view = albedoSpecImgView;

	return albedoSpecImg;
}

static Image createDepthAttachment(const Application& app) {
	auto depthImg = createImage(app,
		app.swapChain.extent.width, app.swapChain.extent.height,
		formats::depth,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto depthImgView = createImageView(app, depthImg.handle, depthImg.format, VK_IMAGE_ASPECT_DEPTH_BIT);

	depthImg.view = depthImgView;

	return depthImg;
}

VkFramebuffer createGBufferFramebuffer(const Application& app) {
	const std::array<VkImageView, 4> attachments = {{
		app.gBuffer.position.view,
		app.gBuffer.normal.view,
		app.gBuffer.albedoSpec.view,
		app.gBuffer.depth.view
	}};

	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.renderPass = app.gBuffer.renderPass;
	fbInfo.attachmentCount = attachments.size();
	fbInfo.pAttachments = attachments.data();
	fbInfo.width = app.swapChain.extent.width;
	fbInfo.height = app.swapChain.extent.height;
	fbInfo.layers = 1;

	VkFramebuffer fb;
	VLKCHECK(vkCreateFramebuffer(app.device, &fbInfo, nullptr, &fb));
	app.validation.addObjectInfo(fb, __FILE__, __LINE__);

	return fb;
}

void GBuffer::createAttachments(const Application& app) {
	position = createPosAttachment(app);
	normal = createNormalAttachment(app);
	albedoSpec = createAlbedoSpecAttachment(app);
	depth = createDepthAttachment(app);

	position.sampler = createTextureSampler(app);
	normal.sampler = createTextureSampler(app);
	albedoSpec.sampler = createTextureSampler(app);
	depth.sampler = createTextureSampler(app);
}

VkDescriptorPool createGBufferDescriptorPool(const Application& app) {
	std::array<VkDescriptorPoolSize, 2> poolSizes = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = 1;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = 2;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = poolSizes.size();
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 1;

	VkDescriptorPool descriptorPool;
	VLKCHECK(vkCreateDescriptorPool(app.device, &poolInfo, nullptr, &descriptorPool));
	app.validation.addObjectInfo(descriptorPool, __FILE__, __LINE__);

	return descriptorPool;
}

VkDescriptorSetLayout createGBufferDescriptorSetLayout(const Application& app) {
	// texDiffuse: sampler2D
	VkDescriptorSetLayoutBinding texDiffuseLayoutBinding = {};
	texDiffuseLayoutBinding.binding = 0;
	texDiffuseLayoutBinding.descriptorCount = 1;
	texDiffuseLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	texDiffuseLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// texSpecular: sampler2D
	VkDescriptorSetLayoutBinding texSpecularLayoutBinding = {};
	texSpecularLayoutBinding.binding = 1;
	texSpecularLayoutBinding.descriptorCount = 1;
	texSpecularLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	texSpecularLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// ubo: { model, view, proj }
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 2;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	const std::array<VkDescriptorSetLayoutBinding, 3> bindings = {
		texDiffuseLayoutBinding,
		texSpecularLayoutBinding,
		uboLayoutBinding,
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

VkPipelineLayout createGBufferPipelineLayout(const Application& app) {

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &app.gBuffer.descriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 0;
	pipelineLayoutInfo.pPushConstantRanges = nullptr;

	VkPipelineLayout pipelineLayout;
	VLKCHECK(vkCreatePipelineLayout(app.device, &pipelineLayoutInfo, nullptr, &pipelineLayout));
	app.validation.addObjectInfo(pipelineLayout, __FILE__, __LINE__);

	return pipelineLayout;
}

VkDescriptorSet createGBufferDescriptorSet(const Application& app, VkDescriptorSetLayout descriptorSetLayout,
		const Buffer& uniformBuffer, const Image& texDiffuseImage, const Image& texSpecularImage)
{
	const std::array<VkDescriptorSetLayout, 1> layouts = { descriptorSetLayout };
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = app.gBuffer.descriptorPool;
	allocInfo.descriptorSetCount = layouts.size();
	allocInfo.pSetLayouts = layouts.data();

	VkDescriptorSet descriptorSet;
	VLKCHECK(vkAllocateDescriptorSets(app.device, &allocInfo, &descriptorSet));
	app.validation.addObjectInfo(descriptorSet, __FILE__, __LINE__);

	VkDescriptorBufferInfo bufferInfo = {};
	bufferInfo.buffer = uniformBuffer.handle;
	bufferInfo.offset = 0;
	bufferInfo.range = uniformBuffer.size;

	VkDescriptorImageInfo texDiffuseInfo = {};
	texDiffuseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	texDiffuseInfo.imageView = texDiffuseImage.view;
	texDiffuseInfo.sampler = texDiffuseImage.sampler;

	VkDescriptorImageInfo texSpecularInfo = {};
	texSpecularInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	texSpecularInfo.imageView = texSpecularImage.view;
	texSpecularInfo.sampler = texSpecularImage.sampler;

	std::array<VkWriteDescriptorSet, 3> descriptorWrites = {};
	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pImageInfo = &texDiffuseInfo;

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = descriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].pImageInfo = &texSpecularInfo;

	descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[2].dstSet = descriptorSet;
	descriptorWrites[2].dstBinding = 2;
	descriptorWrites[2].dstArrayElement = 0;
	descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[2].descriptorCount = 1;
	descriptorWrites[2].pBufferInfo = &bufferInfo;

	vkUpdateDescriptorSets(app.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	return descriptorSet;
}

VkPipeline createGBufferPipeline(const Application& app) {
	auto vertShaderModule = createShaderModule(app, "shaders/gbuffer.vert.spv");
	auto fragShaderModule = createShaderModule(app, "shaders/gbuffer.frag.spv");

	VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertShaderModule;
	vertShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";

	const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
		vertShaderStageInfo,
		fragShaderStageInfo
	};

	// Configure fixed pipeline
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	const auto bindingDescription = Vertex::getBindingDescription();
	const auto attributeDescriptions = Vertex::getAttributeDescriptions();
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size();
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport = {};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = static_cast<float>(app.swapChain.extent.width);
	viewport.height = static_cast<float>(app.swapChain.extent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	VkRect2D scissor = {};
	scissor.offset = {0, 0};
	scissor.extent = app.swapChain.extent;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
	colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
					| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachmentState.blendEnable = VK_FALSE;
	colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

	const std::array<VkPipelineColorBlendAttachmentState, 3> colorBlendAttachmentStates = {
		colorBlendAttachmentState,
		colorBlendAttachmentState,
		colorBlendAttachmentState,
	};

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = colorBlendAttachmentStates.size();
	colorBlending.pAttachments = colorBlendAttachmentStates.data();

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.stencilTestEnable = VK_FALSE;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = shaderStages.size();
	pipelineInfo.pStages = shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pDynamicState = nullptr;
	pipelineInfo.layout = app.gBuffer.pipelineLayout;
	pipelineInfo.renderPass = app.gBuffer.renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

	VkPipeline pipeline;
	VLKCHECK(vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));
	app.validation.addObjectInfo(pipeline, __FILE__, __LINE__);

	// Cleanup
	vkDestroyShaderModule(app.device, fragShaderModule, nullptr);
	vkDestroyShaderModule(app.device, vertShaderModule, nullptr);

	return pipeline;
}

VkCommandBuffer createGBufferCommandBuffer(const Application& app, uint32_t nIndices,
		const Buffer& vertexBuffer, const Buffer& indexBuffer, const Buffer& uniformBuffer,
		VkDescriptorSet descSet)
{
	std::array<VkClearValue, 4> clearValues = {};
	clearValues[0].color = {{ 0, 0, 0, 0 }};
	clearValues[1].color = {{ 0, 0, 0, 0 }};
	clearValues[2].color = {{ 0, 0, 0, 0 }};
	clearValues[3].depthStencil = { 1, 0 };

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = app.gBuffer.renderPass;
	renderPassBeginInfo.framebuffer = app.gBuffer.framebuffer;
	renderPassBeginInfo.renderArea.extent.width = app.swapChain.extent.width;
	renderPassBeginInfo.renderArea.extent.height = app.swapChain.extent.height;
	renderPassBeginInfo.clearValueCount = clearValues.size();
	renderPassBeginInfo.pClearValues = clearValues.data();

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	beginInfo.pInheritanceInfo = nullptr;

	auto commandBuffer = allocCommandBuffer(app, app.commandPool);
	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.gBuffer.pipeline);

	const std::array<VkBuffer, 1> vertexBuffers = { vertexBuffer.handle };
	const std::array<VkDeviceSize, 1> offsets = { 0 };
	static_assert(vertexBuffers.size() == offsets.size(),
			"offsets should be the same amount of vertexBuffers!");
	vkCmdBindVertexBuffers(commandBuffer, 0, vertexBuffers.size(),
			vertexBuffers.data(), offsets.data());
	vkCmdBindIndexBuffer(commandBuffer, indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			app.gBuffer.pipelineLayout, 0, 1, &app.gBuffer.descriptorSet, 0, nullptr);
	vkCmdDrawIndexed(commandBuffer, nIndices, 1, 0, 0, 0);
	vkCmdEndRenderPass(commandBuffer);

	VLKCHECK(vkEndCommandBuffer(commandBuffer));

	return commandBuffer;
}
