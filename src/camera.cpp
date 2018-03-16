#include "camera.hpp"
#include "clock.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

Camera createCamera() {
	Camera camera;
	camera.position = { 0.f, 60.f, -140.f };
	camera.rotation = glm::quat{}; //glm::angleAxis(glm::radians(45.f), glm::vec3{0.f, 1.f, 0.f});
	camera.worldUp = { 0.f, 1.f, 0.f };
	return camera;
}

glm::mat4 Camera::viewMatrix() const {
	//std::cerr << "forward = " << glm::to_string(forward()) << "\n";
	//std::cerr << "lookAt(" << position << ", " << position + glm::vec3{forward()} << ", " << worldUp << ")\n";
	return glm::lookAt(position, position + glm::vec3{forward()}, worldUp);
}

glm::mat4 Camera::projMatrix() const {
	return glm::perspective(glm::radians(fov), ratio, near, far);
}

glm::vec4 Camera::forward() const {
	return glm::rotate(rotation, glm::vec4{ 0.f, 0.f, 1.f, 0.f });
}


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
