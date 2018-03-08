#pragma once

#include "endpoint.hpp"

class ServerEndpoint : public Endpoint {
	void loopFunc() override;
};
