#include "swap.hpp"
#include <limits>
#include <algorithm>
#include <array>
#include "buffers.hpp"
#include "application.hpp"
#include "phys_device.hpp"
#include "shaders.hpp"
#include "vertex.hpp"
#include "vulk_errors.hpp"
#include "images.hpp"
#include "buffers.hpp"
#include <iostream>

// Windows, really...
#undef max
#undef min

using namespace std::literals::string_literals;

static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
	// No preferred format
	if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED)
		return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	for (const auto& availableFormat : availableFormats) {
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM
				&& availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			return availableFormat;
	}

	return availableFormats[0];
}

static VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
	auto bestMode = VK_PRESENT_MODE_FIFO_KHR;

	for (const auto& availablePresentMode : availablePresentModes) {
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
			return availablePresentMode;
		} else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
			bestMode = availablePresentMode;
		}
	}
	return bestMode;
}

static VkExtent2D chooseSwapExtent(GLFWwindow *window, const VkSurfaceCapabilitiesKHR& capabilities) {
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		return capabilities.currentExtent;
	else {
		int width, height;
		glfwGetWindowSize(window, &width, &height);
		VkExtent2D actualExtent = { uint32_t(width), uint32_t(height) };
		actualExtent.width = std::max(capabilities.minImageExtent.width,
						std::min(capabilities.maxImageExtent.width, actualExtent.width));
		actualExtent.height = std::max(capabilities.minImageExtent.height,
						std::min(capabilities.maxImageExtent.height, actualExtent.height));
		return actualExtent;
	}
}

static VkCompositeAlphaFlagBitsKHR chooseCompositeAlphaMode(const SwapChainSupportDetails& support) {
	auto cAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	const std::array<VkCompositeAlphaFlagBitsKHR, 4> flags = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
	};
	for (auto mode : flags)
		if (support.capabilities.supportedCompositeAlpha & mode)
			return mode;
	return cAlpha;
}

void SwapChain::destroyTransient(VkDevice device) {
	for (auto framebuffer : framebuffers)
		vkDestroyFramebuffer(device, framebuffer, nullptr);

	for (auto imageView : imageViews)
		vkDestroyImageView(device, imageView, nullptr);

	depthImage.destroy(device);

	vkDestroySwapchainKHR(device, handle, nullptr);

	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);
}

SwapChain createSwapChain(const Application& app, VkSwapchainKHR oldSwapchain) {
	const auto swapChainSupport = querySwapChainSupport(app.physicalDevice, app.surface);
	const auto surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
	const auto presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
	const auto extent = chooseSwapExtent(app.window, swapChainSupport.capabilities);

	uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
	if (swapChainSupport.capabilities.maxImageCount > 0 &&
			swapChainSupport.capabilities.maxImageCount < imageCount)
	{
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = app.surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	// Note: would probably use TRANSFER_DST_BIT if we didn't render directly to the swap chain
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	// Set additional usage flag for blitting from the swapchain images if supported
	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties(app.physicalDevice, surfaceFormat.format, &formatProps);
	if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)
		createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	auto indices = findQueueFamilies(app.physicalDevice, app.surface);
	const std::array<uint32_t, 2> queueFamilyIndices = {
		static_cast<uint32_t>(indices.graphicsFamily),
		static_cast<uint32_t>(indices.presentFamily)
	};

	if (indices.graphicsFamily != indices.presentFamily) {
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = queueFamilyIndices.size();
		createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
	} else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}
	createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
	createInfo.compositeAlpha = chooseCompositeAlphaMode(swapChainSupport);
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = oldSwapchain;

	VkSwapchainKHR swapChainHandle;
	VLKCHECK(vkCreateSwapchainKHR(app.device, &createInfo, nullptr, &swapChainHandle));

	SwapChain swapChain;
	swapChain.handle = swapChainHandle;
	swapChain.extent = extent;
	swapChain.imageFormat = surfaceFormat.format;

	vkGetSwapchainImagesKHR(app.device, swapChain.handle, &imageCount, nullptr);
	swapChain.images.resize(imageCount);
	vkGetSwapchainImagesKHR(app.device, swapChain.handle, &imageCount, swapChain.images.data());

	return swapChain;
}

std::vector<VkImageView> createSwapChainImageViews(const Application& app) {
	std::vector<VkImageView> imageViews{ app.swapChain.images.size() };

	for (std::size_t i = 0; i < app.swapChain.images.size(); ++i) {
		imageViews[i] = createImageView(app,
			app.swapChain.images[i], app.swapChain.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	return imageViews;
}

std::vector<VkFramebuffer> createSwapChainFramebuffers(const Application& app) {
	std::vector<VkFramebuffer> framebuffers{ app.swapChain.imageViews.size() };

	for (std::size_t i = 0; i < app.swapChain.imageViews.size(); ++i) {
		const std::array<VkImageView, 2> attachments = {
			app.swapChain.imageViews[i],
			app.swapChain.depthImage.view
		};

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = app.swapChain.renderPass;
		framebufferInfo.attachmentCount = attachments.size();
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = app.swapChain.extent.width;
		framebufferInfo.height = app.swapChain.extent.height;
		framebufferInfo.layers = 1;

		VLKCHECK(vkCreateFramebuffer(app.device, &framebufferInfo, nullptr, &framebuffers[i]));
	}

	return framebuffers;
}

uint32_t acquireNextSwapImage(const Application& app, VkSemaphore imageAvailableSemaphore) {
	uint32_t imageIndex;
	const auto result = vkAcquireNextImageKHR(app.device, app.swapChain.handle,
			std::numeric_limits<uint64_t>::max(),
			imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		return -1;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		throw std::runtime_error("failed to acquire swap chain image!");
	}

	return imageIndex;
}

std::vector<VkCommandBuffer> createSwapChainCommandBuffers(const Application& app, VkCommandPool commandPool) {
	std::vector<VkCommandBuffer> commandBuffers{ app.swapChain.framebuffers.size() };

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

	VLKCHECK(vkAllocateCommandBuffers(app.device, &allocInfo, commandBuffers.data()));

	return commandBuffers;
}

void recordSwapChainCommandBuffers(const Application& app, std::vector<VkCommandBuffer>& commandBuffers,
		uint32_t nIndices, const Buffer& uniformBuffer, VkDescriptorSet descriptorSet)
{
	std::array<VkClearValue, 2> clearValues = {};
	clearValues[0].color = {0.f, 0.0f, 0.f, 1.f};
	clearValues[1].depthStencil = {1.f, 0};

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = app.swapChain.renderPass;
	renderPassInfo.renderArea.offset = {0, 0};
	renderPassInfo.renderArea.extent = app.swapChain.extent;
	renderPassInfo.clearValueCount = clearValues.size();
	renderPassInfo.pClearValues = clearValues.data();

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

	for (size_t i = 0; i < commandBuffers.size(); ++i) {
		VLKCHECK(vkBeginCommandBuffer(commandBuffers[i], &beginInfo));

		renderPassInfo.framebuffer = app.swapChain.framebuffers[i];

		vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, app.swapChain.pipeline);
		const std::array<VkBuffer, 1> vertexBuffers = { app.screenQuadBuffer.handle };
		const std::array<VkDeviceSize, 1> offsets = { 0 };
		static_assert(vertexBuffers.size() == offsets.size(),
				"offsets should be the same amount of vertexBuffers!");
		vkCmdBindVertexBuffers(commandBuffers[i], 0, vertexBuffers.size(),
				vertexBuffers.data(), offsets.data());
		vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
				app.res.pipelineLayouts->get("swap"), 0, 1, &descriptorSet, 0, nullptr);
		vkCmdDraw(commandBuffers[i], 4, 1, 0, 0);

		vkCmdEndRenderPass(commandBuffers[i]);

		VLKCHECK(vkEndCommandBuffer(commandBuffers[i]));
	}
}

VkDescriptorSetLayout createSwapChainDescriptorSetLayout(const Application& app) {
	// gPosition: sampler2D
	VkDescriptorSetLayoutBinding gPosLayoutBinding = {};
	gPosLayoutBinding.binding = 0;
	gPosLayoutBinding.descriptorCount = 1;
	gPosLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	gPosLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// gNormal: sampler2D
	VkDescriptorSetLayoutBinding gNormalLayoutBinding = {};
	gNormalLayoutBinding.binding = 1;
	gNormalLayoutBinding.descriptorCount = 1;
	gNormalLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	gNormalLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// gAlbedoSpec: sampler2D
	VkDescriptorSetLayoutBinding gAlbedoSpecLayoutBinding = {};
	gAlbedoSpecLayoutBinding.binding = 2;
	gAlbedoSpecLayoutBinding.descriptorCount = 1;
	gAlbedoSpecLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	gAlbedoSpecLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// ubo: CompositionUniformBufferObject
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 3;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	const std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
		gPosLayoutBinding,
		gNormalLayoutBinding,
		gAlbedoSpecLayoutBinding,
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

VkDescriptorSet createSwapChainDescriptorSet(const Application& app, VkDescriptorSetLayout descriptorSetLayout,
		const Buffer& uniformBuffer, const Image& texDiffuseImage, VkSampler texSampler)
{
	const auto& gPosition = app.gBuffer.position;
	//const auto& gAlbedoSpec = texDiffuseImage;
	const auto& gNormal = app.gBuffer.normal;
	const auto& gAlbedoSpec = app.gBuffer.albedoSpec;

	const std::array<VkDescriptorSetLayout, 1> layouts = { descriptorSetLayout };
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = app.descriptorPool;
	allocInfo.descriptorSetCount = layouts.size();
	allocInfo.pSetLayouts = layouts.data();

	VkDescriptorSet descriptorSet;
	VLKCHECK(vkAllocateDescriptorSets(app.device, &allocInfo, &descriptorSet));
	app.validation.addObjectInfo(descriptorSet, __FILE__, __LINE__);

	VkDescriptorImageInfo gPositionInfo = {};
	gPositionInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	gPositionInfo.imageView = gPosition.view;
	gPositionInfo.sampler = app.gBuffer.sampler;

	VkDescriptorImageInfo gNormalInfo = {};
	gNormalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	gNormalInfo.imageView = gNormal.view;
	gNormalInfo.sampler = app.gBuffer.sampler;

	VkDescriptorImageInfo gAlbedoSpecInfo = {};
	gAlbedoSpecInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	gAlbedoSpecInfo.imageView = gAlbedoSpec.view;
	gAlbedoSpecInfo.sampler = app.gBuffer.sampler;

	VkDescriptorBufferInfo bufferInfo = {};
	bufferInfo.buffer = uniformBuffer.handle;
	bufferInfo.offset = 0;
	bufferInfo.range = uniformBuffer.size;

	std::array<VkWriteDescriptorSet, 4> descriptorWrites = {};

	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pImageInfo = &gPositionInfo;

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = descriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].pImageInfo = &gNormalInfo;

	descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[2].dstSet = descriptorSet;
	descriptorWrites[2].dstBinding = 2;
	descriptorWrites[2].dstArrayElement = 0;
	descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[2].descriptorCount = 1;
	descriptorWrites[2].pImageInfo = &gAlbedoSpecInfo;

	descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[3].dstSet = descriptorSet;
	descriptorWrites[3].dstBinding = 3;
	descriptorWrites[3].dstArrayElement = 0;
	descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[3].descriptorCount = 1;
	descriptorWrites[3].pBufferInfo = &bufferInfo;

	vkUpdateDescriptorSets(app.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	return descriptorSet;
}

VkPipeline createSwapChainPipeline(const Application& app, const std::string& shader) {
	auto vertShaderModule = createShaderModule(app, ("shaders/"s + shader + ".vert.spv").c_str());
	auto fragShaderModule = createShaderModule(app, ("shaders/"s + shader + ".frag.spv").c_str());

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
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	//inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
	rasterizer.cullMode = VK_CULL_MODE_NONE;//VK_CULL_MODE_BACK_BIT;
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

	const std::array<VkPipelineColorBlendAttachmentState, 1> colorBlendAttachmentStates = {
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
	pipelineInfo.layout = app.res.pipelineLayouts->get("swap");
	pipelineInfo.renderPass = app.swapChain.renderPass;
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

VkDescriptorSetLayout createSwapChainDebugDescriptorSetLayout(const Application& app) {
	VkDescriptorSetLayoutBinding uboBinding = {};
	uboBinding.binding = 0;
	uboBinding.descriptorCount = 1;
	uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutBinding texLayoutBinding = {};
	texLayoutBinding.binding = 1;
	texLayoutBinding.descriptorCount = 1;
	texLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	texLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	const std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
		uboBinding,
		texLayoutBinding
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount =  bindings.size();
	layoutInfo.pBindings = bindings.data();

	VkDescriptorSetLayout descriptorSetLayout;
	VLKCHECK(vkCreateDescriptorSetLayout(app.device, &layoutInfo, nullptr, &descriptorSetLayout));
	app.validation.addObjectInfo(descriptorSetLayout, __FILE__, __LINE__);

	return descriptorSetLayout;
}

VkDescriptorSet createSwapChainDebugDescriptorSet(const Application& app,
		VkDescriptorSetLayout descriptorSetLayout, const Buffer& ubo, const Image& tex,
		VkSampler texSampler)
{
	VkDescriptorBufferInfo uboInfo = {};
	uboInfo.buffer = ubo.handle;
	uboInfo.offset = 0;
	uboInfo.range = ubo.size;

	VkDescriptorImageInfo texInfo = {};
	texInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	texInfo.imageView = tex.view;
	texInfo.sampler = texSampler;

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = app.descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &descriptorSetLayout;

	VkDescriptorSet descriptorSet;
	VLKCHECK(vkAllocateDescriptorSets(app.device, &allocInfo, &descriptorSet));
	app.validation.addObjectInfo(descriptorSet, __FILE__, __LINE__);

	std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};
	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pBufferInfo = &uboInfo;

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = descriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].pImageInfo = &texInfo;

	vkUpdateDescriptorSets(app.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	return descriptorSet;
}

void recordSwapChainDebugCommandBuffers(const Application& app, std::vector<VkCommandBuffer>& commandBuffers,
		uint32_t nIndices,
		const Buffer& vertexBuffer, const Buffer& indexBuffer, const Buffer& uniformBuffer,
		VkDescriptorSet descriptorSet)
{
	for (size_t i = 0; i < commandBuffers.size(); ++i) {
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		beginInfo.pInheritanceInfo = nullptr;

		vkBeginCommandBuffer(commandBuffers[i], &beginInfo);

		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = app.swapChain.renderPass;
		renderPassInfo.framebuffer = app.swapChain.framebuffers[i];
		renderPassInfo.renderArea.offset = {0, 0};
		renderPassInfo.renderArea.extent = app.swapChain.extent;
		std::array<VkClearValue, 2> clearValues = {};
		clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
		clearValues[1].depthStencil = {1.f, 0};
		renderPassInfo.clearValueCount = clearValues.size();
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, app.swapChain.pipeline);
		const std::array<VkBuffer, 1> vertexBuffers = { vertexBuffer.handle };
		const std::array<VkDeviceSize, 1> offsets = { 0 };
		static_assert(vertexBuffers.size() == offsets.size(),
				"offsets should be the same amount of vertexBuffers!");
		vkCmdBindVertexBuffers(commandBuffers[i], 0, vertexBuffers.size(),
				vertexBuffers.data(), offsets.data());
		vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
		vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
				app.res.pipelineLayouts->get("swap"), 0, 1, &descriptorSet, 0, nullptr);
		vkCmdDrawIndexed(commandBuffers[i], nIndices, 1, 0, 0, 0);
		//std::cerr << (app.swapChain.screenQuadBuffer.size / sizeof(Vertex)) << "\n";
		//vkCmdDraw(commandBuffers[i], 4, 1, 0, 0);
		//std::cerr << "recreating command buffer with v = "
			//<< nVertices << ", i = " << nIndices << "\n";
		vkCmdEndRenderPass(commandBuffers[i]);

		VLKCHECK(vkEndCommandBuffer(commandBuffers[i]));
	}
}
