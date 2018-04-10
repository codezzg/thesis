#pragma once

#include <vulkan/vulkan.h>

struct Application;

VkFramebuffer createGBuffer(const Application& app);
