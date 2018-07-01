#include "textures.hpp"
#include "application.hpp"
#include "buffers.hpp"
#include "commands.hpp"
#include "formats.hpp"
#include "logging.hpp"
#include "profile.hpp"
#include "utils.hpp"
#include "vulk_errors.hpp"
#include <algorithm>
#include <chrono>
#include <numeric>
#include <vulkan/vulkan.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_GIF
#include "third_party/stb_image.h"

using namespace logging;
using shared::TextureFormat;

void TextureLoader::saveImageInfo(Image& image,
	stbi_uc* pixels,
	int texWidth,
	int texHeight,
	int texChannels,
	TextureFormat format)
{
	debug("Loaded texture with width = ", texWidth, ", height = ", texHeight, " chans = ", texChannels);

	const auto imageSize =
		static_cast<VkDeviceSize>(texWidth) * texHeight * (format == TextureFormat::RGBA ? 4 : 1);
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
}

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

	saveImageInfo(image, pixels, texWidth, texHeight, texChannels, texture.format);

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
		latestError = "Failed to load texture " + texturePath;
		return false;
	}

	saveImageInfo(image, pixels, texWidth, texHeight, texChannels, format);

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
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.levelCount = 1;
	subresourceRange.layerCount = 1;

	VkDeviceSize bufOffset = 0;
	for (unsigned i = 0; i < images.size(); ++i) {
		const auto& info = imageInfos[i];
		const auto imageSize = info.width * info.height * (info.format == VK_FORMAT_R8G8B8A8_UNORM ? 4 : 1);
		auto& textureImage = images[i];

		transitionImageLayout(app,
			textureImage->handle,
			info.format,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			subresourceRange);

		copyBufferToImage(app, stagingBuffer.handle, textureImage->handle, info.width, info.height, bufOffset);
		bufOffset += imageSize;

		transitionImageLayout(app,
			textureImage->handle,
			info.format,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			subresourceRange);

		textureImage->view =
			createImageView(app, textureImage->handle, textureImage->format, VK_IMAGE_ASPECT_COLOR_BIT);
	}
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

VkSampler createTextureCubeSampler(const Application& app)
{
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	samplerInfo.maxAnisotropy = 16;
	samplerInfo.anisotropyEnable = VK_TRUE;

	VkSampler sampler;
	VLKCHECK(vkCreateSampler(app.device, &samplerInfo, nullptr, &sampler));
	app.validation.addObjectInfo(sampler, __FILE__, __LINE__);

	return sampler;
}

Image createTextureCube(const Application& app, const std::array<std::string, 6>& faces)
{
	Image image;
	image.handle = VK_NULL_HANDLE;

	struct TexInfo {
		int width;
		int height;
		int channels;
	};
	std::array<stbi_uc*, 6> pixels;
	std::array<TexInfo, 6> texInfos;
	std::array<std::future<bool>, 6> loadTasks;

	for (int i = 0; i < 6; ++i) {
		loadTasks[i] = std::async(std::launch::async, [&pixels, &texInfos, &faces, i]() {
			pixels[i] = stbi_load(faces[i].c_str(),
				&texInfos[i].width,
				&texInfos[i].height,
				&texInfos[i].channels,
				STBI_rgb_alpha);

			return !!pixels[i];
		});
	}

	info("Loaded images");

	for (int i = 0; i < 6; ++i) {
		if (!loadTasks[i].get()) {
			err("Failed to load texture image for cubemap!");
			return image;
		}
	}

	// TODO check size consistency
	for (int i = 1; i < 6; ++i) {
		if (texInfos[i].width != texInfos[0].width || texInfos[i].height != texInfos[0].height) {
			err("Inconsistent texture size for cubemap!");
			return image;
		}
	}

	image = createImage(app,
		texInfos[0].width,
		texInfos[0].height,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
		6);

	// TODO reuse existing one
	const auto bufsize =
		std::accumulate(std::begin(texInfos), std::end(texInfos), 0, [](auto acc, const auto& info) {
			return acc + info.width * info.height * 4;
		});
	auto stagingBuffer = createStagingBuffer(app, bufsize);

	// Copy pixels to staging buffer
	const auto stride = texInfos[0].width * texInfos[0].height * 4;
	for (int i = 0; i < 6; ++i) {
		memcpy(reinterpret_cast<uint8_t*>(stagingBuffer.ptr) + i * stride, pixels[i], stride);
		stbi_image_free(pixels[i]);
	}

	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.levelCount = 1;
	subresourceRange.layerCount = 6;

	// Copy buffer to image
	transitionImageLayout(app,
		image.handle,
		image.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		subresourceRange);

	VkCommandBuffer commandBuffer = beginSingleTimeCommands(app, app.commandPool);

	std::array<VkBufferImageCopy, 6> regions = {};
	VkDeviceSize bufOffset = 0;
	for (int i = 0; i < 6; ++i) {
		auto& region = regions[i];
		region.bufferOffset = bufOffset;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = i;
		region.imageSubresource.layerCount = 1;
		region.imageExtent.width = texInfos[i].width;
		region.imageExtent.height = texInfos[i].height;
		region.imageExtent.depth = 1;
		bufOffset += stride;
	}

	vkCmdCopyBufferToImage(commandBuffer,
		stagingBuffer.handle,
		image.handle,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		regions.size(),
		regions.data());

	endSingleTimeCommands(app.device, app.queues.graphics, app.commandPool, commandBuffer);

	transitionImageLayout(app,
		image.handle,
		image.format,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		subresourceRange);

	image.view = createImageCubeView(app, image.handle, image.format, VK_IMAGE_ASPECT_COLOR_BIT);

	destroyBuffer(app.device, stagingBuffer);

	return image;
}

