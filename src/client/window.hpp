#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

class VulkanClient;

GLFWwindow* initWindow();

void cleanupWindow(GLFWwindow* window);

std::vector<const char*> getRequiredExtensions(bool validationEnabled);
