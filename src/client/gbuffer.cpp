#include "gbuffer.hpp"
#include "application.hpp"
#include "images.hpp"
#include "vulk_errors.hpp"
#include <array>

VkFramebuffer createGBuffer(const Application& app) {
	auto positionImg = createImage(app,
		app.swapChain.extent.width, app.swapChain.extent.height,
		VK_FORMAT_R8G8B8_SNORM,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto normalImg = createImage(app,
		app.swapChain.extent.width, app.swapChain.extent.height,
		VK_FORMAT_R8G8B8_SNORM,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto albedoSpecImg = createImage(app,
		app.swapChain.extent.width, app.swapChain.extent.height,
		VK_FORMAT_R8G8B8A8_UINT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	auto positionImgView = createImageView(app, positionImg.handle,
			positionImg.format, VK_IMAGE_ASPECT_COLOR_BIT);
	auto normalImgView = createImageView(app, normalImg.handle,
			normalImg.format, VK_IMAGE_ASPECT_COLOR_BIT);
	auto albedoSpecImgView = createImageView(app, albedoSpecImg.handle,
			albedoSpecImg.format,
			VK_IMAGE_ASPECT_COLOR_BIT);

	const std::array<VkImageView, 3> attachments = {
		positionImgView,
		normalImgView,
		albedoSpecImgView
	};

	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.attachmentCount = attachments.size();
	fbInfo.pAttachments = attachments.data();
	fbInfo.width = app.swapChain.extent.width;
	fbInfo.height = app.swapChain.extent.height;
	fbInfo.layers = 1;

	VkFramebuffer gBuffer;
	VLKCHECK(vkCreateFramebuffer(app.device, &fbInfo, nullptr, &gBuffer));

	return gBuffer;
}
