#include "gbuffer.hpp"
#include "application.hpp"
#include "images.hpp"
#include "vulk_errors.hpp"
#include "formats.hpp"
#include "shaders.hpp"
#include "vertex.hpp"
#include <array>

static Image createPosAttachment(const Application& app) {
	auto bestPosFormat = findSupportedFormat(app.physicalDevice, {
		VK_FORMAT_R32G32B32_SFLOAT,
		VK_FORMAT_R32G32B32A32_SFLOAT,
	}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
	auto positionImg = createImage(app,
		app.swapChain.extent.width, app.swapChain.extent.height,
		bestPosFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto positionImgView = createImageView(app, positionImg.handle,
			positionImg.format, VK_IMAGE_ASPECT_COLOR_BIT);

	positionImg.view = positionImgView;

	return positionImg;
}


static Image createNormalAttachment(const Application& app) {
	auto bestNormFormat = findSupportedFormat(app.physicalDevice, {
		VK_FORMAT_R8G8B8_UNORM,
		VK_FORMAT_R8G8B8A8_UNORM,
	}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
	auto normalImg = createImage(app,
		app.swapChain.extent.width, app.swapChain.extent.height,
		bestNormFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto normalImgView = createImageView(app, normalImg.handle,
			normalImg.format, VK_IMAGE_ASPECT_COLOR_BIT);

	normalImg.view = normalImgView;

	return normalImg;
}

static Image createAlbedoSpecAttachment(const Application& app) {
	auto bestASFormat = findSupportedFormat(app.physicalDevice, {
		VK_FORMAT_R32G32B32A32_SFLOAT,
	}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
	auto albedoSpecImg = createImage(app,
		app.swapChain.extent.width, app.swapChain.extent.height,
		bestASFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto albedoSpecImgView = createImageView(app, albedoSpecImg.handle,
			albedoSpecImg.format,
			VK_IMAGE_ASPECT_COLOR_BIT);

	albedoSpecImg.view = albedoSpecImgView;

	return albedoSpecImg;
}

static Image createDepthAttachment(const Application& app) {
	auto depthFormat = findDepthFormat(app.physicalDevice);
	auto depthImg = createImage(app,
		app.swapChain.extent.width, app.swapChain.extent.height,
		depthFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto depthImgView = createImageView(app, depthImg.handle, depthImg.format, VK_IMAGE_ASPECT_DEPTH_BIT);

	depthImg.view = depthImgView;

	return depthImg;
}

std::vector<Image> createGBufferAttachments(const Application& app) {
	auto position = createPosAttachment(app);
	auto normal = createNormalAttachment(app);
	auto albedoSpec = createAlbedoSpecAttachment(app);
	auto depth = createDepthAttachment(app);

	return { position, normal, albedoSpec, depth };
}

GBuffer createGBuffer(const Application& app, const std::vector<Image>& attachments) {

	std::vector<VkImageView> attachViews{ attachments.size() };
	for (unsigned i = 0; i < attachments.size(); ++i)
		attachViews[i] = attachments[i].view;

	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.renderPass = app.geomRenderPass;
	fbInfo.attachmentCount = attachViews.size();
	fbInfo.pAttachments = attachViews.data();
	fbInfo.width = app.swapChain.extent.width;
	fbInfo.height = app.swapChain.extent.height;
	fbInfo.layers = 1;

	VkFramebuffer gBufferHandle;
	VLKCHECK(vkCreateFramebuffer(app.device, &fbInfo, nullptr, &gBufferHandle));

	GBuffer gBuffer;
	gBuffer.handle = gBufferHandle;
	gBuffer.attachments = attachments;

	return gBuffer;
}

VkDescriptorSetLayout createGBufferDescriptorSetLayout(const Application& app) {
	// ubo: { model, view, proj }
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	// texDiffuse: sampler2D
	VkDescriptorSetLayoutBinding texDiffuseLayoutBinding = {};
	texDiffuseLayoutBinding.binding = 1;
	texDiffuseLayoutBinding.descriptorCount = 1;
	texDiffuseLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	texDiffuseLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// texSpecular: sampler2D
	VkDescriptorSetLayoutBinding texSpecularLayoutBinding = {};
	texSpecularLayoutBinding.binding = 2;
	texSpecularLayoutBinding.descriptorCount = 1;
	texSpecularLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	texSpecularLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	std::array<VkDescriptorSetLayoutBinding, 3> bindings = {
		uboLayoutBinding,
		texDiffuseLayoutBinding,
		texSpecularLayoutBinding,
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = bindings.size();
	layoutInfo.pBindings = bindings.data();

	VkDescriptorSetLayout descriptorSetLayout;
	VLKCHECK(vkCreateDescriptorSetLayout(app.device, &layoutInfo, nullptr, &descriptorSetLayout));

	return descriptorSetLayout;
}

std::pair<VkPipeline, VkPipelineLayout> createGBufferPipeline(const Application& app) {
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

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
					| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	const std::array<VkPipelineColorBlendAttachmentState, 3> colorBlendAttachments = {
		colorBlendAttachment,
		colorBlendAttachment,
		colorBlendAttachment,
	};

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = colorBlendAttachments.size();
	colorBlending.pAttachments = colorBlendAttachments.data();

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.stencilTestEnable = VK_FALSE;

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &app.gBuffer.descriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 0;
	pipelineLayoutInfo.pPushConstantRanges = nullptr;

	VkPipelineLayout pipelineLayout;
	VLKCHECK(vkCreatePipelineLayout(app.device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

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
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = app.geomRenderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

	VkPipeline pipeline;
	VLKCHECK(vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));

	// Cleanup
	vkDestroyShaderModule(app.device, fragShaderModule, nullptr);
	vkDestroyShaderModule(app.device, vertShaderModule, nullptr);

	return std::make_pair(pipeline, pipelineLayout);
}
