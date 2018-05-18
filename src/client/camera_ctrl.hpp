#pragma once

#include <GLFW/glfw3.h>
#include "camera.hpp"

class CameraController {
protected:
	Camera& camera;

public:
	enum Direction { FWD, BACK, RIGHT, LEFT, UP, DOWN };

	float cameraSpeed = 50.f;
	float sensitivity = 0.15f;

	explicit CameraController(Camera& camera) : camera{ camera } {}

	void move(Direction dir);
	virtual void turn(double xoff, double yoff) = 0;

	virtual void processInput(GLFWwindow *window) = 0;
};

class FPSCameraController : public CameraController {
public:
	explicit FPSCameraController(Camera& camera) : CameraController{ camera } {}

	void turn(double xoff, double yoff) override;

	void processInput(GLFWwindow *window) override;
};


/** A camera that can be moved along axes and does not follow mouse. */
class CubeCameraController : public CameraController {
public:
	explicit CubeCameraController(Camera& camera) : CameraController{ camera } {}

	void turn(double xoff, double yoff) override;

	void processInput(GLFWwindow *window) override;
};
