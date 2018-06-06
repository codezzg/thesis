#pragma once

#include "buffers.hpp"
#include "images.hpp"
#include "shared_resources.hpp"
#include <vector>
#include <vulkan/vulkan.h>

struct Application;

// TODO
class TextureLoader final {
	struct ImageInfo {
		VkFormat format;
		uint32_t width;
		uint32_t height;
	};

	Buffer& stagingBuffer;
	std::size_t stagingBufferOffset = 0;

	std::vector<ImageInfo> imageInfos;
	std::vector<Image*> images;

public:
	explicit TextureLoader(Buffer& stagingBuffer)
	        : stagingBuffer{ stagingBuffer }
	{
	}

	/** Load a texture from raw data pointed by `texture` */
	void addTexture(Image& image, const shared::Texture& texture);
	/** Load a texture from file with given format */
	void addTexture(Image& image, const char* texturePath, shared::TextureFormat format);

	void create(const Application& app);
};

/** Load a texture from `texturePath` into an Image and return it (with its view already filled).
 *  `stagingBuffer` must be a valid buffer to use as a staging buffer.
 */
Image createTextureImage(const Application& app,
        const char* texturePath,
        shared::TextureFormat format,
        Buffer& stagingBuffer);

/** Create a sampler appropriate for sampling a texture and return it. */
VkSampler createTextureSampler(const Application& app);
