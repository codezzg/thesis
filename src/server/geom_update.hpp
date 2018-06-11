#pragma once

#include "udp_messages.hpp"
#include <vector>

struct Model;

std::vector<udp::ChunkHeader> buildUpdatePackets(const Model& model);
