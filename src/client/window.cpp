#include "window.hpp"
#include "client.hpp"
#include "config.hpp"
#include "validation.hpp"

extern bool gLimitFrameTime;

GLFWwindow* initWindow()
{
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	auto window = glfwCreateWindow(cfg::WIDTH, cfg::HEIGHT, "Vulkan", nullptr, nullptr);

	return window;
}

void cleanupWindow(GLFWwindow* window)
{
	glfwDestroyWindow(window);
	glfwTerminate();
}

std::vector<const char*> getRequiredExtensions(bool validationEnabled)
{
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector<const char*> extensions{ glfwExtensions, glfwExtensions + glfwExtensionCount };

	if (validationEnabled)
		extensions.emplace_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

	return extensions;
}

void cbCursorMoved(GLFWwindow* window, double xpos, double ypos)
{
	constexpr double centerX = cfg::WIDTH / 2.0, centerY = cfg::HEIGHT / 2.0;
	static bool firstTime = true;

	if (!firstTime) {
		auto appl = reinterpret_cast<VulkanClient*>(glfwGetWindowUserPointer(window));
		appl->cameraCtrl->turn(xpos - centerX, centerY - ypos);
	}
	firstTime = false;

	// info("xpos = ", xpos, " xoff = ", xpos - center);
	// centerX = xpos;
	// centerY = ypos;
	glfwSetCursorPos(window, centerX, centerY);
}

void cbKeyPressed(GLFWwindow* window, int key, int /*scancode*/, int action, int)
{
	if (action != GLFW_PRESS)
		return;

	auto appl = reinterpret_cast<VulkanClient*>(glfwGetWindowUserPointer(window));
	switch (key) {
	case GLFW_KEY_Q:
		appl->disconnect();
		glfwSetWindowShouldClose(window, GLFW_TRUE);
		break;
	case GLFW_KEY_G:
		appl->shaderOpts.flip(ShaderOpts::SHOW_GBUF_TEX);
		break;
	case GLFW_KEY_N:
		appl->shaderOpts.flip(ShaderOpts::USE_NORMAL_MAP);
		break;
	case GLFW_KEY_T:
		gLimitFrameTime = !gLimitFrameTime;
		break;
	case GLFW_KEY_KP_ADD:
		appl->cameraCtrl->cameraSpeed += 10;
		break;
	case GLFW_KEY_KP_SUBTRACT:
		appl->cameraCtrl->cameraSpeed -= 10;
		break;
	case GLFW_KEY_F4: {
		// Toggle windowed fullscreen
		const auto mode = glfwGetVideoMode(appl->app.monitor);
		if (appl->fullscreen) {
			glfwSetWindowMonitor(window, nullptr, 100, 100, cfg::WIDTH, cfg::HEIGHT, mode->refreshRate);
		} else {
			glfwSetWindowMonitor(window, nullptr, 0, 0, mode->width, mode->height, mode->refreshRate);
		}
		appl->fullscreen = !appl->fullscreen;
	} break;
	default:
		break;
	}
}

