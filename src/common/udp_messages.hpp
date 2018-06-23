#pragma once

#include "config.hpp"
#include "hashing.hpp"
#include <array>
#include <cstddef>
#include <ostream>

enum class UdpMsgType : uint8_t {
	/** A GeomUpdatePacket, which modifies a model's vertices or indices */
	GEOM_UPDATE = 0x01,
	/** A PointLightUpdatePacket, which modifies a light's position and/or color and/or intensity */
	POINT_LIGHT_UPDATE = 0x02,
	UNKNOWN
};

inline std::ostream& operator<<(std::ostream& s, UdpMsgType msg)
{
	switch (msg) {
		using M = UdpMsgType;
	case M::GEOM_UPDATE:
		s << "GEOM_UPDATE";
		break;
	case M::POINT_LIGHT_UPDATE:
		s << "POINT_LIGHT_UPDATE";
		break;
	default:
		s << "UNKNOWN";
		break;
	}
	return s;
}

constexpr UdpMsgType byte2udpmsg(uint8_t byte)
{
	return byte == 0 || byte > static_cast<uint8_t>(UdpMsgType::UNKNOWN) ? UdpMsgType::UNKNOWN
									     : static_cast<UdpMsgType>(byte);
}

constexpr uint8_t udpmsg2byte(UdpMsgType type)
{
	return type == UdpMsgType::UNKNOWN ? 0 : static_cast<uint8_t>(type);
}

enum class GeomDataType : uint8_t {
	VERTEX = 0,
	INDEX = 1,
	INVALID = 2,
};

#pragma pack(push, 1)

struct UdpHeader {
	/** Sequential packet "generation" id */
	uint64_t packetGen;
	/** How many bytes of the payload are actual data (as there may be garbage at the end).
	 *  Must be equal to the sum of all the chunks' size (type + header + payload).
	 */
	uint32_t size;
};

/** A single UDP packet. The format is the following:
 *  [udp header] (containing packet generation and total payload size)
 *  [chunk0 type] (containing the type of the next chunk header + payload)
 *  [chunk0 header] (the metadata about its payload)
 *  [chunk0 payload] (the actual data)
 *  [chunk1 type]
 *  ...
 *  Note that a UdpPacket can contain different types of chunks.
 */
struct UdpPacket {
	UdpHeader header;
	/** Payload contains chunks (each consisting of ChunkHeader + chunk payload) */
	std::array<uint8_t, cfg::PACKET_SIZE_BYTES - sizeof(UdpHeader)> payload;
};

struct GeomUpdateHeader {
	StringId modelId;
	GeomDataType dataType;
	/** Starting vertex/index to modify */
	uint32_t start;
	/** Amount of vertices/indices to modify */
	uint32_t len;
};

struct PointLightUpdateHeader {
	StringId lightId;
	/** Defines what kind of updates follow. It must be equal to the light's dynmask and uses the same flags. */
	uint8_t updateMask;
};

#pragma pack(pop)

static_assert(sizeof(UdpPacket) == cfg::PACKET_SIZE_BYTES, "sizeof(GeomUpdatePacket) != PACKET_SIZE_BYTES!");
