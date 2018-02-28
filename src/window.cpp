#include "window.hpp"
#include "config.hpp"
#include "validation.hpp"

GLFWwindow* initWindow() {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	auto window = glfwCreateWindow(cfg::WIDTH, cfg::HEIGHT, "Vulkan", nullptr, nullptr);

	glfwSetKeyCallback(window, [] (GLFWwindow *window, int key, int /*scancode*/, int action, int) {
		if (key == GLFW_KEY_Q && action == GLFW_PRESS)
			glfwSetWindowShouldClose(window, GLFW_TRUE);
	});

	return window;
}

void cleanupWindow(GLFWwindow* window) {
	glfwDestroyWindow(window);
	glfwTerminate();
}

std::vector<const char*> getRequiredExtensions(bool validationEnabled) {
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector<const char*> extensions { glfwExtensions, glfwExtensions + glfwExtensionCount };

	if (validationEnabled)
		extensions.emplace_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

	return extensions;
}

