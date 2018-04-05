#pragma once

#include <GLFW/glfw3.h>
#include "camera.hpp"

class CameraController {
	Camera& camera;

public:
	enum Direction { FWD, BACK, RIGHT, LEFT };

	float cameraSpeed = 100.f;
	float sensitivity = 0.005f;

	explicit CameraController(Camera& camera) : camera(camera) {}

	void move(Direction dir);
	void turn(double xoff, double yoff);

	void processInput(GLFWwindow *window);
};
