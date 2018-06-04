#include "vulk_utils.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		throw std::runtime_error("failed to open file " + filename + "!");

	const auto fileSize = static_cast<std::size_t>(file.tellg());
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	return buffer;
}

void dumpPhysicalDevice(VkPhysicalDevice& physicalDevice) {
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	std::cout << "Picked physical device: " << props.deviceName << std::endl;
}
