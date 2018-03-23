#pragma once

#include "endpoint.hpp"

class ClientPassiveEndpoint : public Endpoint {
	uint8_t *buffer = nullptr;
	volatile bool bufferFilled = false;
	volatile int64_t frameId = -1;

	void loopFunc() override;

public:
	const uint8_t* peek() const;
	int64_t getFrameId() const { return frameId; }
};

class ClientActiveEndpoint : public Endpoint {
	void loopFunc() override;
};
