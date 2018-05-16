#include "textures.hpp"
#include "application.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
#include "formats.hpp"
#include "buffers.hpp"
#include "logging.hpp"
#include "vulk_errors.hpp"
#include <vulkan/vulkan.h>

Image createTextureImage(const Application& app, const char *texturePath, TextureFormat format) {
	int texWidth, texHeight, texChannels;
	auto pixels = stbi_load(texturePath, &texWidth, &texHeight, &texChannels,
			format == TextureFormat::RGBA
				? STBI_rgb_alpha
				: STBI_grey);

	if (!pixels)
		throw std::runtime_error("failed to load texture image!");

	VkDeviceSize imageSize = texWidth * texHeight * (format == TextureFormat::RGBA ? 4 : 1);

	// Load the texture into a buffer
	const auto stagingBuffer = createBuffer(app, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	void *data;
	VLKCHECK(vkMapMemory(app.device, stagingBuffer.memory, 0, imageSize, 0, &data));
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	vkUnmapMemory(app.device, stagingBuffer.memory);

	stbi_image_free(pixels);

	const auto vkFormat = format == TextureFormat::RGBA ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8_UNORM;

	auto textureImage = createImage(app, texWidth, texHeight,
			vkFormat, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	textureImage.view = createImageView(app, textureImage.handle,
			textureImage.format, VK_IMAGE_ASPECT_COLOR_BIT);

	// Transfer the loaded buffer into the image
	transitionImageLayout(app, textureImage.handle, vkFormat,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	copyBufferToImage(app, stagingBuffer.handle, textureImage.handle, texWidth, texHeight);
	transitionImageLayout(app, textureImage.handle, vkFormat,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	vkDestroyBuffer(app.device, stagingBuffer.handle, nullptr);
	vkFreeMemory(app.device, stagingBuffer.memory, nullptr);

	return textureImage;
}

VkSampler createTextureSampler(const Application& app) {
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	VkSampler sampler;
	VLKCHECK(vkCreateSampler(app.device, &samplerInfo, nullptr, &sampler));
	app.validation.addObjectInfo(sampler, __FILE__, __LINE__);

	return sampler;
}
