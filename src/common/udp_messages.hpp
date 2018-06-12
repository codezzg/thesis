#pragma once

#include "config.hpp"
#include "hashing.hpp"
#include <array>
#include <cstddef>

namespace udp {

enum class DataType : uint8_t {
	VERTEX = 0,
	INDEX = 1,
	INVALID = 2,
};

/* Description of the UDP data sent by the server to update geometry.
 * Update packets have this format:
 * [0] header.magic (4)
 * [4] header.packetGen (8)
 * [12] prelude.size (4)
 * [16] chunk0.modelId (4)
 * [20] chunk0.dataType (1)
 * [21] chunk0.start (4)
 * [25] chunk0.len (4)
 * [29] chunk0.payload (chunk0.len * sizeof(<datatype>))
 * ...
 */

#pragma pack(push, 1)

struct Header {
	/** Must be cfg::PACKET_MAGIC */
	uint32_t magic;
	/** Sequential packet "generation" id */
	uint64_t packetGen;
	/** How many bytes of the payload are actual data (as there may be garbage at the end).
	 *  Must be equal to the sum of all the chunks' `len`.
	 */
	uint32_t size;
};

struct ChunkHeader {
	StringId modelId;
	DataType dataType;
	uint32_t start;
	uint32_t len;
};

struct UpdatePacket {
	Header header;
	/** Payload contains chunks (each consisting of ChunkHeader + chunk payload) */
	std::array<uint8_t, cfg::PACKET_SIZE_BYTES - sizeof(Header)> payload;
};

#pragma pack(pop)

}   // namespace udp

static_assert(sizeof(udp::UpdatePacket) == cfg::PACKET_SIZE_BYTES, "sizeof(UpdatePacket) != PACKET_SIZE_BYTES!");
