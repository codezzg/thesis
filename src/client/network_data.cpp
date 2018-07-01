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

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

using namespace logging;

/** Tries to read a GeomUpdate chunk from given `ptr`.
 *  Will not read more than `maxBytesToRead`.
 *  If a chunk is correctly read from the buffer, its content is interpreted and used to
 *  create a new UpdateReq inside `updateReqs`.
 *  @return The number of bytes read
 */
static std::size_t readGeomUpdateChunk(const uint8_t* ptr,
	std::size_t maxBytesToRead,
	const Geometry& geometry,
	std::vector<UpdateReq>& updateReqs)
{
	if (maxBytesToRead <= sizeof(GeomUpdateHeader)) {
		err("Buffer given to readGeomUpdateChunk has not enough room for a Header + Payload!");
		return maxBytesToRead;
	}

	//// Read the chunk header
	const auto header = reinterpret_cast<const GeomUpdateHeader*>(ptr);

	UpdateReq req = {};
	req.type = UpdateReq::Type::GEOM;
	req.data.geom.modelId = header->modelId;

	std::size_t dataSize = 0;
	switch (header->dataType) {
	case GeomDataType::VERTEX:
		dataSize = sizeof(Vertex);
		req.data.geom.src = ptr + sizeof(GeomUpdateHeader);
		req.data.geom.dst = geometry.vertexBuffer.ptr;
		break;
	case GeomDataType::INDEX:
		dataSize = sizeof(Index);
		req.data.geom.src = ptr + sizeof(GeomUpdateHeader);
		req.data.geom.dst = geometry.indexBuffer.ptr;
		break;
	default: {
		std::stringstream ss;
		ss << "Invalid data type " << int(header->dataType) << " in Update Chunk!";
		throw std::runtime_error(ss.str());
	} break;
	}

	assert(dataSize != 0 && req.data.geom.dst != nullptr);

	req.data.geom.nBytes = header->len * dataSize;

	const auto chunkSize = sizeof(GeomUpdateHeader) + dataSize * header->len;

	if (chunkSize > maxBytesToRead) {
		err("readGeomUpdateChunk would read past the allowed memory area!");
		return maxBytesToRead;
	}

	auto it = geometry.locations.find(header->modelId);
	if (it == geometry.locations.end()) {
		warn("Received an Update Chunk for inexistent model ", header->modelId, "!");
		// XXX
		return chunkSize;
	}

	auto& loc = it->second;
	// Use the correct offset into the vertex/index buffer
	const auto baseOffset = header->dataType == GeomDataType::VERTEX ? loc.vertexOff : loc.indexOff;
	req.data.geom.dst = reinterpret_cast<uint8_t*>(req.data.geom.dst) + baseOffset;
	req.data.geom.dst = reinterpret_cast<uint8_t*>(req.data.geom.dst) + header->start * dataSize;

	{   // Ensure we don't write past the buffers area
		const auto ptrStart = reinterpret_cast<uintptr_t>(req.data.geom.dst);
		const auto actualPtrStart = reinterpret_cast<uintptr_t>(header->dataType == GeomDataType::VERTEX
										? geometry.vertexBuffer.ptr
										: geometry.indexBuffer.ptr);
		const auto ptrLen =
			actualPtrStart + (header->dataType == GeomDataType::VERTEX ? geometry.vertexBuffer.size
										   : geometry.indexBuffer.size);
		verbose("writing at offset ", std::hex, ptrStart, " / ", actualPtrStart, " / ", ptrLen);
		assert(actualPtrStart <= ptrStart && ptrStart <= ptrLen - dataSize * header->len);
	}

	assert(req.type == UpdateReq::Type::GEOM);
	assert(req.data.geom.modelId != SID_NONE);
	assert(req.data.geom.src);
	assert(req.data.geom.dst);
	assert(req.data.geom.nBytes > 0);

	updateReqs.emplace_back(req);

	return chunkSize;
}

/** Tries to read a PointLightUpdate chunk from `ptr`.
 *  Won't try to read more than `maxBytesToRead`.
 *  In case of success, an UpdateReq is added to `updateReqs`.
 *  @return The number of bytes read from `ptr`.
 */
static std::size_t
	readPointLightUpdateChunk(const uint8_t* ptr, std::size_t maxBytesToRead, std::vector<UpdateReq>& updateReqs)
{
	if (maxBytesToRead < sizeof(PointLightUpdateHeader)) {
		err("Buffer given to rocessPointLightUpdateChunk has not enough room for a Header + Payload!");
		return maxBytesToRead;
	}

	//// Read header
	const auto header = reinterpret_cast<const PointLightUpdateHeader*>(ptr);
	const auto chunkSize = sizeof(PointLightUpdateHeader);

	if (chunkSize > maxBytesToRead) {
		err("readPointLightUpdateChunk would read past the allowed memory area!");
		return maxBytesToRead;
	}

	UpdateReq req = {};
	req.type = UpdateReq::Type::POINT_LIGHT;
	req.data.pointLight.lightId = header->lightId;
	req.data.pointLight.color = header->color;
	req.data.pointLight.intensity = header->intensity;

	assert(req.type == UpdateReq::Type::POINT_LIGHT);
	updateReqs.emplace_back(req);

	return chunkSize;
}

/** Tries to read a TransformUpdate chunk from `ptr`.
 *  Won't try to read more than `maxBytesToRead`.
 *  In case of success, an updateReq is added to `updateReqs`.
 *  @return The number of bytes read from `ptr`.
 */
static std::size_t
	readTransformUpdateChunk(const uint8_t* ptr, std::size_t maxBytesToRead, std::vector<UpdateReq>& updateReqs)
{
	if (maxBytesToRead < sizeof(TransformUpdateHeader)) {
		err("Buffer given to readTransformUpdateChunk has not enough room for a Header + Payload! ",
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

	if (chunkSize > maxBytesToRead) {
		err("readTransformUpdateChunk would read past the allowed memory area!");
		return maxBytesToRead;
	}

	UpdateReq req = {};
	req.type = UpdateReq::Type::TRANSFORM;
	req.data.transform.objectId = header->objectId;
	req.data.transform.transform = header->transform;

	assert(req.type == UpdateReq::Type::TRANSFORM);
	assert(req.data.transform.objectId != SID_NONE);
	updateReqs.emplace_back(req);

	return chunkSize;
}

/** Receives a pointer to a byte buffer and tries to read a chunk from it.
 *  Will not try to read more than `maxBytesToRead` bytes from the buffer.
 *  @return The number of bytes read, (aka the offset of the next chunk if there are more chunks after this)
 */
static std::size_t readChunk(const uint8_t* ptr,
	std::size_t maxBytesToRead,
	const Geometry& geometry,
	std::vector<UpdateReq>& updateReqs)
{
	//// Read the chunk type
	static_assert(sizeof(UdpMsgType) == 1, "Need to change this code!");

	switch (byte2udpmsg(ptr[0])) {

	case UdpMsgType::GEOM_UPDATE:
		return sizeof(UdpMsgType) +
		       readGeomUpdateChunk(
			       ptr + sizeof(UdpMsgType), maxBytesToRead - sizeof(UdpMsgType), geometry, updateReqs);

	case UdpMsgType::POINT_LIGHT_UPDATE:
		return sizeof(UdpMsgType) + readPointLightUpdateChunk(ptr + sizeof(UdpMsgType),
						    maxBytesToRead - sizeof(UdpMsgType),
						    updateReqs);

	case UdpMsgType::TRANSFORM_UPDATE:
		return sizeof(UdpMsgType) + readTransformUpdateChunk(ptr + sizeof(UdpMsgType),
						    maxBytesToRead - sizeof(UdpMsgType),
						    updateReqs);
	default:
		break;
	}

	err("Invalid chunk type ", int(ptr[0]));
	return maxBytesToRead;
}

void receiveData(ClientPassiveEndpoint& passiveEP,
	std::vector<uint8_t>& buffer,
	const Geometry& geometry,
	std::vector<UpdateReq>& updateReqs)
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
		const auto bytesInChunk = readChunk(buffer.data() + bytesProcessed, bytesLeft, geometry, updateReqs);
		++nChunksProcessed;
		verbose("bytes in chunk: ", bytesInChunk);
		bytesLeft -= bytesInChunk;
		bytesProcessed += bytesInChunk;
		assert(bytesLeft >= 0);
	}
	debug("Processed ", nChunksProcessed, " chunks.");
}

void updateModel(const UpdateReqGeom& req)
{
	verbose("Copying from ",
		std::hex,
		uintptr_t(req.src),
		" --> ",
		uintptr_t(req.dst),
		std::dec,
		"  (",
		req.nBytes,
		")");

	// Do the actual update
	memcpy(req.dst, req.src, req.nBytes);
}

void updatePointLight(const UpdateReqPointLight& req, NetworkResources& netRsrc)
{
	// Find the referenced light
	auto it = std::find_if(netRsrc.pointLights.begin(),
		netRsrc.pointLights.end(),
		[name = req.lightId](const auto light) { return light.name == name; });
	if (it == netRsrc.pointLights.end()) {
		warn("Received an Update Chunk for inexistent pointLight ", req.lightId, "!");
		return;
	}

	it->color = req.color;
	it->intensity = req.intensity;
}

void updateTransform(const UpdateReqTransform& req, ObjectTransforms& transforms)
{
	// Find the referenced object
	// info("transforms = ", mapToString(transforms, [](auto x) -> std::string { return glm::to_string(x); }));

	auto it = transforms.find(req.objectId);
	if (it == transforms.end()) {
		warn("Received a Transform Update Chunk for inexistent node ", req.objectId, "!");
		return;
	}

	//// Update the transform
	it->second = req.transform;
}
