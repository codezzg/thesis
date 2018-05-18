#include "camera_ctrl.hpp"
#include "clock.hpp"
#include <cmath>
#include <glm/gtx/string_cast.hpp>
#include "logging.hpp"

#ifndef M_PI
	#define M_PI 3.14159265358979323846264338327950288
#endif

using namespace logging;

void CameraController::move(CameraController::Direction dir) {
	const auto dt = Clock::instance().deltaTime();

	switch (dir) {
		using D = CameraController::Direction;
	case D::FWD:
		camera.position += camera.front * cameraSpeed * dt;
		break;
	case D::BACK:
		camera.position -= camera.front * cameraSpeed * dt;
		break;
	case D::RIGHT:
		camera.position += camera.right * cameraSpeed * dt;
		break;
	case D::LEFT:
		camera.position -= camera.right * cameraSpeed * dt;
		break;
	case D::UP:
		camera.position += camera.worldUp * cameraSpeed * dt;
		break;
	case D::DOWN:
		camera.position -= camera.worldUp * cameraSpeed * dt;
		break;
	}

	verbose("cam pos = ", glm::to_string(camera.position));
}

void FPSCameraController::turn(double xoff, double yoff) {
	xoff *= sensitivity;
	yoff *= sensitivity;

	camera.yaw += xoff;
	camera.pitch += yoff;

	if (camera.pitch > 89.f)
		camera.pitch = 89.f;
	else if (camera.pitch < -89.f)
		camera.pitch = -89.f;

	verbose("cam yaw = ", camera.yaw, ", pitch = ", camera.pitch);
	camera.updateVectors();
}

void FPSCameraController::processInput(GLFWwindow *window) {
	if (glfwGetKey(window, GLFW_KEY_W))
		move(Direction::FWD);
	if (glfwGetKey(window, GLFW_KEY_A))
		move(Direction::LEFT);
	if (glfwGetKey(window, GLFW_KEY_S))
		move(Direction::BACK);
	if (glfwGetKey(window, GLFW_KEY_D))
		move(Direction::RIGHT);
}

//// Cube

void CubeCameraController::turn(double, double) {}

void CubeCameraController::processInput(GLFWwindow *window) {
	if (glfwGetKey(window, GLFW_KEY_W))
		move(Direction::UP);
	if (glfwGetKey(window, GLFW_KEY_A))
		move(Direction::LEFT);
	if (glfwGetKey(window, GLFW_KEY_S))
		move(Direction::DOWN);
	if (glfwGetKey(window, GLFW_KEY_D))
		move(Direction::RIGHT);
	if (glfwGetKey(window, GLFW_KEY_R))
		move(Direction::FWD);
	if (glfwGetKey(window, GLFW_KEY_F))
		move(Direction::BACK);
}



