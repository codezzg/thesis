#pragma once

#include "logging.hpp"
#include <cstddef>
#include <cstdint>

struct GeomUpdateHeader;
struct ServerResources;
namespace shared {
struct PointLight;
}

/** Writes an UDP header into `buffer`.
 *  @return The amount of bytes written.
 */
std::size_t writeUdpHeader(uint8_t* buffer, std::size_t bufsize, uint64_t packetGen);

/** Writes a geometry update chunk (including the header) into `buffer`, starting at `offset`.
 *  @return the number of bytes written, or 0 if the buffer hadn't enough room.
 */
std::size_t addGeomUpdate(uint8_t* buffer,
	std::size_t bufsize,
	std::size_t offset,
	const GeomUpdateHeader& geomUpdate,
	const ServerResources& resources);

/** Writes a point light update chunk (including the header) into `buffer`, starting at `offset`.
 *  @return the number of bytes written, or 0 if the buffer hadn't enough room.
 */
std::size_t addPointLightUpdate(uint8_t* buffer,
	std::size_t bufsize,
	std::size_t offset,
	const shared::PointLight& pointLight);

void dumpFullPacket(const uint8_t* buffer, std::size_t bufsize, LogLevel loglv);
