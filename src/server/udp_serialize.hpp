#pragma once

#include "logging.hpp"
#include <cstddef>
#include <cstdint>

struct GeomUpdateHeader;
struct Server;
struct QueuedUpdate;
namespace shared {
struct PointLight;
}

/** Writes an UDP header into `buffer`.
 *  @return The amount of bytes written.
 */
std::size_t writeUdpHeader(uint8_t* buffer, std::size_t bufsize, uint64_t packetGen);

/** Transforms a generic queued update into an udp update chunk and writes it into `buffer`, starting at `offset`.
 *  @return the number of bytes written, or 0 if the buffer hadn't enough room.
 */
std::size_t addUpdate(uint8_t* buffer,
	std::size_t bufsize,
	std::size_t offset,
	const QueuedUpdate* update,
	const Server& server);

void dumpFullPacket(const uint8_t* buffer, std::size_t bufsize, LogLevel loglv);
