#pragma once

#include <array>
#include "config.hpp"
#include "vertex.hpp"

#pragma pack(push, 1)
struct FrameHeader final {
	/** Must be equal to cfg::PACKET_MAGIC */
	uint32_t magic;
	/** Valid count starts from 0 */
	int64_t frameId;
	/** Valid count starts from 0 */
	int32_t packetId;

	uint64_t nVertices;
	uint64_t nIndices;
};

/* A FrameData is the data sent from the server to the client each frame.
 * For convenience and efficiency, payload size is a multiple of both sizeof(Index) and sizeof(Vertex).
 */
struct FrameData final {
	FrameHeader header;
	std::array<uint8_t, cfg::PACKET_SIZE_BYTES - sizeof(FrameHeader)> payload;
};
#pragma pack(pop)


static_assert(sizeof(FrameHeader) == 32, "FrameHeader should be 32 bytes!");
static_assert(sizeof(FrameData) == cfg::PACKET_SIZE_BYTES, "Unexpected FrameData size!");
static_assert((cfg::PACKET_SIZE_BYTES - sizeof(FrameHeader)) % sizeof(Vertex) == 0,
			"Payload size is not a multiple of sizeof(Vertex)!");
static_assert(sizeof(Vertex) % sizeof(Index) == 0, "sizeof(Vertex) is not a multiple of sizeof(Index)!");
