#include "network_data.hpp"
#include "client_endpoint.hpp"
#include "client_resources.hpp"
#include "geometry.hpp"
#include "logging.hpp"
#include "shared_resources.hpp"
#include "udp_messages.hpp"
#include "utils.hpp"
#include "vertex.hpp"
#include <algorithm>
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
		err("processGeomUpdateChunk would read past the allowed memory area!");
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

	// Do the actual update
	memcpy(dataPtr, ptr + sizeof(GeomUpdateHeader), dataSize * header->len);

	return chunkSize;
}

/** Tries to read a PointLightUpdate chunk from `ptr`.
 *  Won't try to read more than `maxBytesToRead`.
 *  In case of success, the corresponding point light is updated accordingly.
 *  @return The number of bytes read from `ptr`.
 */
static std::size_t
	processPointLightUpdateChunk(const uint8_t* ptr, std::size_t maxBytesToRead, NetworkResources& netRsrc)
{
	using shared::isLightColorFixed;
	using shared::isLightIntensityFixed;
	using shared::isLightPositionFixed;

	if (maxBytesToRead <= sizeof(PointLightUpdateHeader)) {
		err("Buffer given to processPointLightUpdateChunk has not enough room for a Header + Payload!");
		return maxBytesToRead;
	}

	//// Read header
	const auto header = reinterpret_cast<const PointLightUpdateHeader*>(ptr);

	// Figure out chunk size.
	// Payload is:
	// [vec3] newPosition (if position is dynamic in updateMask)
	// [vec3] newColor (if color is dynamic in updateMask)
	// [float] newIntensity (if intensity is dynamic in updateMask)
	std::size_t chunkSize = sizeof(PointLightUpdateHeader);
	const auto mask = header->updateMask;
	if (mask == 0) {
		warn("Received empty PointLight update for light ", header->lightId, "!");
		return chunkSize;
	}
	if (!isLightPositionFixed(mask))
		chunkSize += 3 * sizeof(float);
	if (!isLightColorFixed(mask))
		chunkSize += 3 * sizeof(float);
	if (!isLightIntensityFixed(mask))
		chunkSize += sizeof(float);

	// Find the referenced light
	auto it = std::find_if(netRsrc.pointLights.begin(),
		netRsrc.pointLights.end(),
		[name = header->lightId](const auto light) { return light.name == name; });
	if (it == netRsrc.pointLights.end()) {
		warn("Received an Update Chunk for inexistent pointLight ", header->lightId, "!");
		// XXX
		return chunkSize;
	}

	if (chunkSize > maxBytesToRead) {
		err("processPointLightUpdateChunk would read past the allowed memory area!");
		return maxBytesToRead;
	}

	//// Update the light
	auto payloadPtr = ptr + sizeof(PointLightUpdateHeader);
	if (!isLightPositionFixed(mask)) {
		const auto newPos = *reinterpret_cast<const glm::vec3*>(payloadPtr);
		it->position = newPos;
		payloadPtr += sizeof(glm::vec3);
	}
	if (!isLightColorFixed(mask)) {
		const auto newCol = *reinterpret_cast<const glm::vec3*>(payloadPtr);
		it->color = newCol;
		payloadPtr += sizeof(glm::vec3);
	}
	if (!isLightIntensityFixed(mask)) {
		const auto newInt = *reinterpret_cast<const float*>(payloadPtr);
		it->intensity = newInt;
		payloadPtr += sizeof(float);
	}

	return chunkSize;
}

/** Tries to read a TransformUpdate chunk from `ptr`.
 *  Won't try to read more than `maxBytesToRead`.
 *  In case of success, the corresponding object is updated accordingly.
 *  @return The number of bytes read from `ptr`.
 */
static std::size_t
	processTransformUpdateChunk(const uint8_t* ptr, std::size_t maxBytesToRead, NetworkResources& netRsrc)
{
	if (maxBytesToRead < sizeof(TransformUpdateHeader)) {
		err("Buffer given to processTransformUpdateChunk has not enough room for a Header + Payload! ",
			"(needed: ",
			sizeof(TransformUpdateHeader),
			", got: ",
			maxBytesToRead,
			")");
		return maxBytesToRead;
	}

	//// Read header (which is the entire chunk)
	const auto header = reinterpret_cast<const TransformUpdateHeader*>(ptr);
	const auto chunkSize = sizeof(TransformUpdateHeader);

	// Find the referenced object
	auto it = netRsrc.modelTransforms.find(header->objectId);
	if (it == netRsrc.modelTransforms.end()) {
		warn("Received a Transform Update Chunk for inexistent node ", header->objectId, "!");
		// XXX
		return chunkSize;
	}

	if (chunkSize > maxBytesToRead) {
		err("processTransformUpdateChunk would read past the allowed memory area!");
		return maxBytesToRead;
	}

	//// Update the transform
	it->second = header->transform;

	return chunkSize;
}

/** Receives a pointer to a byte buffer and tries to read a chunk from it.
 *  Will not try to read more than `maxBytesToRead` bytes from the buffer.
 *  @return The number of bytes read, (aka the offset of the next chunk if there are more chunks after this)
 */
static std::size_t
	processChunk(const uint8_t* ptr, std::size_t maxBytesToRead, Geometry& geometry, NetworkResources& netRsrc)
{
	//// Read the chunk type
	static_assert(sizeof(UdpMsgType) == 1, "Need to change this code!");

	switch (byte2udpmsg(ptr[0])) {

	case UdpMsgType::GEOM_UPDATE:
		return sizeof(UdpMsgType) +
		       processGeomUpdateChunk(ptr + sizeof(UdpMsgType), maxBytesToRead - sizeof(UdpMsgType), geometry);

	case UdpMsgType::POINT_LIGHT_UPDATE:
		return sizeof(UdpMsgType) + processPointLightUpdateChunk(ptr + sizeof(UdpMsgType),
						    maxBytesToRead - sizeof(UdpMsgType),
						    netRsrc);

	case UdpMsgType::TRANSFORM_UPDATE:
		return sizeof(UdpMsgType) + processTransformUpdateChunk(ptr + sizeof(UdpMsgType),
						    maxBytesToRead - sizeof(UdpMsgType),
						    netRsrc);
	default:
		break;
	}

	err("Invalid chunk type ", int(ptr[0]));
	return maxBytesToRead;
}

void receiveData(ClientPassiveEndpoint& passiveEP,
	std::vector<uint8_t>& buffer,
	Geometry& geometry,
	NetworkResources& netRsrc)
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
		const auto bytesInChunk = processChunk(buffer.data() + bytesProcessed, bytesLeft, geometry, netRsrc);
		++nChunksProcessed;
		verbose("bytes in chunk: ", bytesInChunk);
		bytesLeft -= bytesInChunk;
		bytesProcessed += bytesInChunk;
		assert(bytesLeft >= 0);
	}
	debug("Processed ", nChunksProcessed, " chunks.");
}
