#include "network_data.hpp"
#include "client_endpoint.hpp"
#include "client_resources.hpp"
#include "geometry.hpp"
#include "logging.hpp"
#include "udp_messages.hpp"
#include "utils.hpp"
#include "vertex.hpp"
#include <cassert>

using namespace logging;

/** Tries to read a GeomUpdate chunk from given `ptr`.
 *  Will not read more than `maxBytesToRead`.
 *  If a chunk is correctly read from the buffer, its content is interpreted and used to
 *  update the proper vertices or indices of a model.
 *  @return The number of bytes read
 */
static std::size_t processGeomUpdateChunk(const uint8_t* ptr, std::size_t maxBytesToRead, Geometry& geometry)
{
	if (maxBytesToRead <= sizeof(GeomUpdateHeader)) {
		err("Buffer given to processGeomUpdateChunk has not enough room for a Header + Payload!");
		return maxBytesToRead;
	}

	//// Read the chunk header
	const auto header = reinterpret_cast<const GeomUpdateHeader*>(ptr);

	std::size_t dataSize = 0;
	void* dataPtr = nullptr;
	switch (header->dataType) {
	case GeomDataType::VERTEX:
		dataSize = sizeof(Vertex);
		dataPtr = geometry.vertexBuffer.ptr;
		break;
	case GeomDataType::INDEX:
		dataSize = sizeof(Index);
		dataPtr = geometry.indexBuffer.ptr;
		break;
	default: {
		std::stringstream ss;
		ss << "Invalid data type " << int(header->dataType) << " in Update Chunk!";
		throw std::runtime_error(ss.str());
	} break;
	}

	assert(dataSize != 0 && dataPtr != nullptr);

	const auto chunkSize = sizeof(GeomUpdateHeader) + dataSize * header->len;

	if (chunkSize > maxBytesToRead) {
		err("processChunk would read past the allowed memory area!");
		return maxBytesToRead;
	}

	auto it = geometry.locations.find(header->modelId);
	if (it == geometry.locations.end()) {
		warn("Received an Update Chunk for inexistent model ", header->modelId, "!");
		// XXX
		return chunkSize;
	}

	//// Update the model

	verbose("Updating model ",
		header->modelId,
		" / (type = ",
		int(header->dataType),
		") from ",
		header->start,
		" to ",
		header->start + header->len);
	auto& loc = it->second;
	// Use the correct offset into the vertex/index buffer
	const auto baseOffset = header->dataType == GeomDataType::VERTEX ? loc.vertexOff : loc.indexOff;
	dataPtr = reinterpret_cast<uint8_t*>(dataPtr) + baseOffset;
	dataPtr = reinterpret_cast<uint8_t*>(dataPtr) + header->start * dataSize;

	{
		// Ensure we don't write past the buffers area
		const auto ptrStart = reinterpret_cast<uintptr_t>(dataPtr);
		const auto actualPtrStart = reinterpret_cast<uintptr_t>(header->dataType == GeomDataType::VERTEX
										? geometry.vertexBuffer.ptr
										: geometry.indexBuffer.ptr);
		const auto ptrLen =
			actualPtrStart + (header->dataType == GeomDataType::VERTEX ? geometry.vertexBuffer.size
										   : geometry.indexBuffer.size);
		verbose("writing at offset ", std::hex, ptrStart, " / ", actualPtrStart, " / ", ptrLen);
		assert(actualPtrStart <= ptrStart && ptrStart <= ptrLen - dataSize * header->len);
	}

	if (header->dataType == GeomDataType::INDEX) {
		Index maxIdx = 0;
		for (unsigned i = 0; i < header->len; ++i) {
			auto idx = reinterpret_cast<const Index*>(ptr + sizeof(GeomUpdateHeader))[i];
			if (idx > maxIdx)
				maxIdx = idx;
		}
		verbose("max idx of chunk at location ", uintptr_t(ptr), " = ", maxIdx);
	}

	verbose("Copying from ",
		std::hex,
		uintptr_t(ptr + sizeof(GeomUpdateHeader)),
		" --> ",
		uintptr_t(dataPtr),
		std::dec,
		"  (",
		dataSize * header->len,
		")");
	dumpBytes(ptr + sizeof(GeomUpdateHeader), dataSize * header->len, 50, LOGLV_UBER_VERBOSE);
	dumpBytes(ptr + sizeof(GeomUpdateHeader), dataSize * header->len, 50, LOGLV_UBER_VERBOSE);
	memcpy(dataPtr, ptr + sizeof(GeomUpdateHeader), dataSize * header->len);

	return chunkSize;
}

static std::size_t processPointLightUpdateChunk(const uint8_t* ptr, std::size_t maxBytesToRead)
{
	return maxBytesToRead;
}

/** Receives a pointer to a byte buffer and tries to read a chunk from it.
 *  Will not try to read more than `maxBytesToRead` bytes from the buffer.
 *  @return The number of bytes read, (aka the offset of the next chunk if there are more chunks after this)
 */
static std::size_t processChunk(const uint8_t* ptr, std::size_t maxBytesToRead, Geometry& geometry)
{
	//// Read the chunk type
	static_assert(sizeof(UdpMsgType) == 1, "Need to change this code!");

	switch (byte2udpmsg(ptr[0])) {
	case UdpMsgType::GEOM_UPDATE:
		return sizeof(UdpMsgType) +
		       processGeomUpdateChunk(ptr + sizeof(UdpMsgType), maxBytesToRead - sizeof(UdpMsgType), geometry);
	case UdpMsgType::POINT_LIGHT_UPDATE:
		return sizeof(UdpMsgType) +
		       processPointLightUpdateChunk(ptr + sizeof(UdpMsgType), maxBytesToRead - sizeof(UdpMsgType));
	default:
		break;
	}

	err("Invalid chunk type ", int(ptr[0]));
	return maxBytesToRead;
}

void receiveData(ClientPassiveEndpoint& passiveEP, std::vector<uint8_t>& buffer, Geometry& geometry)
{
	if (!passiveEP.dataAvailable())
		return;

	auto totBytes = passiveEP.retreive(buffer.data(), buffer.size());

	verbose("BYTES READ (", totBytes, ") = ");
	dumpBytes(buffer.data(), buffer.size(), 50, LOGLV_UBER_VERBOSE);

	// buffer now contains [chunk0|chunk1|...]

	int64_t bytesLeft = totBytes;
	assert(bytesLeft <= static_cast<int64_t>(buffer.size()));

	unsigned bytesProcessed = 0;
	unsigned nChunksProcessed = 0;
	while (bytesProcessed < totBytes) {
		verbose("Processing chunk at offset ", bytesProcessed);
		const auto bytesInChunk = processChunk(buffer.data() + bytesProcessed, bytesLeft, geometry);
		++nChunksProcessed;
		verbose("bytes in chunk: ", bytesInChunk);
		bytesLeft -= bytesInChunk;
		bytesProcessed += bytesInChunk;
		assert(bytesLeft >= 0);
	}
	debug("Processed ", nChunksProcessed, " chunks.");
}
