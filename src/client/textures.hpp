#pragma once

#include <vulkan/vulkan.h>
#include "images.hpp"

struct Application;

/** Load a texture from `texturePath` into an Image and return it (with its view already filled) */
Image createTextureImage(const Application& app, const char *texturePath);

/** Create a sampler appropriate for sampling a texture and return it. */
VkSampler createTextureSampler(const Application& app);
