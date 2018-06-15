#pragma once

#include "udp_messages.hpp"
#include <vector>

struct Model;

/** Given a model, returns a list of ChunkHeaders describing the portions of that model
 *  to be updated. The chunks are built taking the max packet size into account, so they
 *  will all fit an UpdatePacket.
 */
std::vector<udp::ChunkHeader> buildUpdatePackets(const Model& model);
