#include "camera_ctrl.hpp"
#include "clock.hpp"
#include <cmath>

#ifndef M_PI
	#define M_PI 3.14159265358979323846264338327950288
#endif

void CameraController::move(CameraController::Direction dir) {
	const auto dt = Clock::instance().deltaTime();
	const auto fwd = glm::vec3{ camera.forward() };
	const auto right = glm::cross(fwd, camera.worldUp);

	switch (dir) {
		using D = CameraController::Direction;
	case D::FWD:
		camera.position += fwd * cameraSpeed * dt;
		break;
	case D::BACK:
		camera.position -= fwd * cameraSpeed * dt;
		break;
	case D::RIGHT:
		camera.position += right * cameraSpeed * dt;
		break;
	case D::LEFT:
		camera.position -= right * cameraSpeed * dt;
		break;
	}
}

void CameraController::turn(double xoff, double yoff) {
	auto euler = glm::eulerAngles(camera.rotation);
	euler.y += sensitivity * xoff;
	euler.x += sensitivity * yoff;

	//std::cerr << "offset = " << xoff << ", " << yoff << "\n";
	if (euler.x > M_PI)
		euler.x = M_PI;
	else if (euler.x < -M_PI)
		euler.x = -M_PI;
	camera.rotation = glm::quat(euler);
}

void CameraController::processInput(GLFWwindow *window) {
	if (glfwGetKey(window, GLFW_KEY_W))
		move(Direction::FWD);
	if (glfwGetKey(window, GLFW_KEY_A))
		move(Direction::LEFT);
	if (glfwGetKey(window, GLFW_KEY_S))
		move(Direction::BACK);
	if (glfwGetKey(window, GLFW_KEY_D))
		move(Direction::RIGHT);
}
