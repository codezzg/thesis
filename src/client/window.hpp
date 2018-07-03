#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

GLFWwindow* initWindow();
void cleanupWindow(GLFWwindow* window);

std::vector<const char*> getRequiredExtensions(bool validationEnabled);

// GLFW callbacks
void cbCursorMoved(GLFWwindow* window, double xpos, double ypos);
void cbKeyPressed(GLFWwindow* window, int key, int /*scancode*/, int action, int);
