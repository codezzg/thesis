#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

std::vector<char> readFile(const std::string& filename);

void dumpPhysicalDevice(VkPhysicalDevice& physicalDevice);
