#pragma once

#include "images.hpp"
#include <vector>
#include <vulkan/vulkan.h>

struct Application;

/** Creates the skybox (currently with fixed textures) and returns it.
 *  In case of failure, the returned Image will have its handle set to VK_NULL_HANDLE.
 */
Image createSkybox(const Application& app);

std::vector<VkDescriptorSetLayout> createSkyboxDescriptorSetLayouts(const Application& app);
std::vector<VkDescriptorSet> createSkyboxDescriptorSets(const Application& app);
