#pragma once

#include <vulkan/vulkan.h>
#include "images.hpp"

struct Application;

enum class TextureFormat {
	RGBA,
	GREY
};

/** Load a texture from `texturePath` into an Image and return it (with its view already filled) */
Image createTextureImage(const Application& app, const char *texturePath, TextureFormat format);

/** Create a sampler appropriate for sampling a texture and return it. */
VkSampler createTextureSampler(const Application& app);
