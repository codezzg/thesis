#include "vulk_utils.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

void dumpPhysicalDevice(VkPhysicalDevice& physicalDevice)
{
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	std::cout << "Picked physical device: " << props.deviceName << std::endl;
}
