#pragma once

#include <vulkan/vulkan.h>
#include "shared_resources.hpp"

struct NetworkResources;

struct Material {
	// All these handles are unowned.
	VkImageView diffuse;
	VkImageView specular;
	VkDescriptorSet descriptorSet;
};

/** Creates a Material from a shared::Material.
 *  This basically means to associate actual Vulkan image handles (taken from `netRsrc`) to it.
 *  If the needed textures are not found in `netRsrc`, the default ones are assigned.
 *  This function does NOT create the descriptorSet.
 */
Material createMaterial(const shared::Material& mat, const NetworkResources& netRsrc);
