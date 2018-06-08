#include "textures.hpp"
#include "application.hpp"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_GIF
#include "buffers.hpp"
#include "formats.hpp"
#include "logging.hpp"
#include "profile.hpp"
#include "third_party/stb_image.h"
#include "utils.hpp"
#include "vulk_errors.hpp"
#include <chrono>
#include <vulkan/vulkan.h>

using namespace logging;
using shared::TextureFormat;

bool TextureLoader::addTexture(Image& image, const shared::Texture& texture)
{
	int texWidth, texHeight, texChannels;

	verbose("texture.data = ", texture.data);
	dumpBytes(texture.data, texture.size, 50, LOGLV_VERBOSE);

	stbi_uc* pixels = nullptr;
	measure_ms("Load Texture", LOGLV_DEBUG, [&]() {
		pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(texture.data),
			texture.size,
			&texWidth,
			&texHeight,
			&texChannels,
			texture.format == TextureFormat::RGBA ? STBI_rgb_alpha : STBI_grey);
	});

	if (!pixels) {
		std::lock_guard<std::mutex> lock{ mtx };
		latestError = "Failed to load image at offset " + stagingBufferOffset;
		return false;
	}

	debug("Loaded texture with width = ", texWidth, ", height = ", texHeight, " chans = ", texChannels);

	const auto imageSize = texWidth * texHeight * (texture.format == TextureFormat::RGBA ? 4 : 1);
	assert(imageSize > 0);

	ImageInfo info;
	info.format = texture.format == TextureFormat::RGBA ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8_UNORM;
	info.width = texWidth;
	info.height = texHeight;
	{
		std::lock_guard<std::mutex> lock{ mtx };

		// Save pixel data in the staging buffer
		memcpy(reinterpret_cast<uint8_t*>(stagingBuffer.ptr) + stagingBufferOffset, pixels, imageSize);
		stbi_image_free(pixels);

		stagingBufferOffset += imageSize;
		imageInfos.emplace_back(info);
		images.emplace_back(&image);
	}
	return true;
}

std::future<bool> TextureLoader::addTextureAsync(Image& image, const shared::Texture& texture)
{
	return std::async(std::launch::async, [&]() { return addTexture(image, texture); });
}

bool TextureLoader::addTexture(Image& image, const std::string& texturePath, TextureFormat format)
{
	int texWidth, texHeight, texChannels;

	stbi_uc* pixels = nullptr;
	measure_ms("Load Texture", LOGLV_DEBUG, [&]() {
		pixels = stbi_load(texturePath.c_str(),
			&texWidth,
			&texHeight,
			&texChannels,
			format == TextureFormat::RGBA ? STBI_rgb_alpha : STBI_grey);
	});

	if (!pixels) {
		std::lock_guard<std::mutex> lock{ mtx };
		std::stringstream err;
		err << "Failed to load texture " << texturePath;
		latestError = err.str();
		return false;
	}

	const auto imageSize = texWidth * texHeight * (format == TextureFormat::RGBA ? 4 : 1);
	assert(imageSize > 0);

	ImageInfo info;
	info.format = format == TextureFormat::RGBA ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8_UNORM;
	info.width = texWidth;
	info.height = texHeight;
	{
		std::lock_guard<std::mutex> lock{ mtx };

		// Save pixel data in the staging buffer
		memcpy(reinterpret_cast<uint8_t*>(stagingBuffer.ptr) + stagingBufferOffset, pixels, imageSize);
		stbi_image_free(pixels);

		stagingBufferOffset += imageSize;
		imageInfos.emplace_back(info);
		images.emplace_back(&image);
	}

	return true;
}

std::future<bool> TextureLoader::addTextureAsync(Image& image, const std::string& texturePath, TextureFormat format)
{
	// NOTE: capturing `texturePath` by copy, as it gets destroyed when captured by ref
	return std::async(std::launch::async,
		[this, &image, texturePath, format]() { return addTexture(image, texturePath, format); });
}

void TextureLoader::create(const Application& app)
{
	// Create the needed images
	ImageAllocator imgAlloc;
	for (unsigned i = 0; i < imageInfos.size(); ++i) {
		const auto& info = imageInfos[i];
		imgAlloc.addImage(*images[i],
			info.width,
			info.height,
			info.format,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}
	imgAlloc.create(app);

	// Fill the images with pixel data from the staging buffer and create image views
	VkDeviceSize bufOffset = 0;
	for (unsigned i = 0; i < images.size(); ++i) {
		const auto& info = imageInfos[i];
		const auto imageSize = info.width * info.height * (info.format == VK_FORMAT_R8G8B8A8_UNORM ? 4 : 1);
		auto& textureImage = images[i];

		transitionImageLayout(app,
			textureImage->handle,
			info.format,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		copyBufferToImage(app, stagingBuffer.handle, textureImage->handle, info.width, info.height, bufOffset);
		bufOffset += imageSize;

		transitionImageLayout(app,
			textureImage->handle,
			info.format,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		textureImage->view =
			createImageView(app, textureImage->handle, textureImage->format, VK_IMAGE_ASPECT_COLOR_BIT);
	}
}

Image createTextureImage(const Application& app, const char* texturePath, TextureFormat format, Buffer& stagingBuffer)
{
	int texWidth, texHeight, texChannels;
	auto pixels = stbi_load(texturePath,
		&texWidth,
		&texHeight,
		&texChannels,
		format == TextureFormat::RGBA ? STBI_rgb_alpha : STBI_grey);

	if (!pixels)
		throw std::runtime_error("failed to load texture image!");

	VkDeviceSize imageSize = texWidth * texHeight * (format == TextureFormat::RGBA ? 4 : 1);

	// Load the texture into the staging buffer
	memcpy(stagingBuffer.ptr, pixels, static_cast<size_t>(imageSize));

	// Free the host memory
	stbi_image_free(pixels);

	const auto vkFormat = format == TextureFormat::RGBA ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8_UNORM;

	auto textureImage = createImage(app,
		texWidth,
		texHeight,
		vkFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	textureImage.view = createImageView(app, textureImage.handle, textureImage.format, VK_IMAGE_ASPECT_COLOR_BIT);

	// Transfer the loaded buffer into the image
	transitionImageLayout(
		app, textureImage.handle, vkFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	copyBufferToImage(app, stagingBuffer.handle, textureImage.handle, texWidth, texHeight);
	transitionImageLayout(app,
		textureImage.handle,
		vkFormat,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	return textureImage;
}

VkSampler createTextureSampler(const Application& app)
{
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
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
