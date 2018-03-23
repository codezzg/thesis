#pragma once

#include <array>
#include "config.hpp"

#pragma pack(push, 1)
struct FrameHeader final {
	// Must be equal to cfg::PACKET_MAGIC
	uint32_t magic;
	int64_t frameId;
	int32_t packetId;
};

struct FirstFrameData final {
	FrameHeader header;
	uint64_t nVertices;
	uint64_t nIndices;
	std::array<uint8_t, cfg::PACKET_SIZE_BYTES - sizeof(FrameHeader) - 2 * sizeof(uint64_t)> payload;
};

struct FrameData final {
	FrameHeader header;
	std::array<uint8_t, cfg::PACKET_SIZE_BYTES - sizeof(FrameHeader)> payload;
};

#pragma pack(pop)
