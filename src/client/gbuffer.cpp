#include "gbuffer.hpp"
#include "application.hpp"
#include "images.hpp"
#include "vulk_errors.hpp"
#include "formats.hpp"
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
		VK_FORMAT_R32G32B32A32_UINT,
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
