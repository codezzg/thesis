#pragma once

#include <vector>
#include <string>
#include <vulkan/vulkan.h>

std::vector<char> readFile(const std::string& filename);

void dumpPhysicalDevice(VkPhysicalDevice& physicalDevice);
