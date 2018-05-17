#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "images.hpp"
#include "buffers.hpp"

struct Application;

enum class TextureFormat {
	RGBA,
	GREY
};

// TODO
class TextureLoader final {
	Buffer stagingBuffer;
	std::vector<Image*> images;

public:
	/** Schedules `image` to be loaded */
	void addTexture(Image& image, const char *texturePath, TextureFormat format);

	void create(const Application& app);
};


/** Load a texture from `texturePath` into an Image and return it (with its view already filled).
 *  `stagingBuffer` must be a valid buffer to use as a staging buffer.
 */
Image createTextureImage(const Application& app, const char *texturePath, TextureFormat format, Buffer& stagingBuffer);

/** Create a sampler appropriate for sampling a texture and return it. */
VkSampler createTextureSampler(const Application& app);
