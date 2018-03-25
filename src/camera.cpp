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
