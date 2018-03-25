#pragma once

#include "camera.hpp"

void serializeCamera(uint8_t *buffer, const Camera& camera);
Camera deserializeCamera(const uint8_t *buffer);
