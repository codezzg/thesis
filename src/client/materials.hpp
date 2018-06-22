#pragma once

#include "shared_resources.hpp"
#include <vulkan/vulkan.h>

struct NetworkResources;

struct Material {
	StringId name;
	// All these handles are unowned.
	VkImageView diffuse;
	VkImageView specular;
	VkImageView normal;
	VkDescriptorSet descriptorSet;
};

/** Creates a Material from a shared::Material.
 *  This basically means to associate actual Vulkan image handles (taken from `netRsrc`) to it.
 *  If the needed textures are not found in `netRsrc`, the default ones are assigned.
 *  This function does NOT create the descriptorSet.
 */
Material createMaterial(const shared::Material& mat, const NetworkResources& netRsrc);
