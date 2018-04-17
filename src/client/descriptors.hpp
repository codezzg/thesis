#pragma once

#include <vulkan/vulkan.h>

struct Application;
struct Buffer;
struct Image;

VkDescriptorPool createDescriptorPool(VkDevice device);
