#include "udp_serialize.hpp"
#include "queued_update.hpp"
#include "server.hpp"
#include "shared_resources.hpp"
#include "spatial.hpp"
#include "udp_messages.hpp"
#include <algorithm>
#include <cassert>
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>

using namespace logging;

std::size_t writeUdpHeader(uint8_t* buffer, std::size_t bufsize, uint32_t packetGen)
{
	assert(bufsize >= sizeof(UdpHeader));

	UdpHeader header;
	header.packetGen = packetGen;
	header.size = 0;

	memcpy(buffer, reinterpret_cast<void*>(&header), sizeof(UdpHeader));

	return sizeof(UdpHeader);
}

static std::size_t addGeomUpdate(uint8_t* buffer,
	std::size_t bufsize,
	std::size_t offset,
	const GeomUpdateHeader& geomUpdate,
	const ServerResources& resources)
{
	verbose("addGeomUpdate(buf=", buffer, ", bufsiz=", bufsize, ", off=", offset, ")");
	// assert(offset < bufsize);
	assert(geomUpdate.modelId != SID_NONE);
	assert(geomUpdate.dataType < GeomDataType::INVALID);

	// Retreive data from the model
	const auto& model_it = resources.models.find(geomUpdate.modelId);
	if (model_it == resources.models.end())
		err("inexisting model ", int(geomUpdate.modelId));
	assert(model_it != resources.models.end());

	void* dataPtr;
	std::size_t dataSize;
	switch (geomUpdate.dataType) {
	case GeomDataType::VERTEX:
		dataPtr = model_it->second.vertices;
		dataSize = sizeof(Vertex);
		break;
	case GeomDataType::INDEX:
		dataPtr = model_it->second.indices;
		dataSize = sizeof(Index);
		break;
	default:
		err("Invalid dataType passed to addGeomUpdate: ", int(geomUpdate.dataType));
		throw;
	};

	const auto payloadSize = dataSize * geomUpdate.len;
	verbose("start: ", geomUpdate.start, ", len: ", geomUpdate.len);
	verbose("offset: ", offset, ", payload size: ", payloadSize, ", bufsize: ", bufsize);
	// Prevent infinite loops
	assert(sizeof(UdpMsgType) + sizeof(GeomUpdateHeader) + payloadSize < bufsize);

	if (offset + sizeof(UdpMsgType) + sizeof(GeomUpdateHeader) + payloadSize > bufsize) {
		verbose("Not enough room!");
		return 0;
	}

	std::size_t written = 0;

	// Write chunk type
	static_assert(sizeof(UdpMsgType) == 1, "Need to change this code!");
	buffer[offset] = udpmsg2byte(UdpMsgType::GEOM_UPDATE);
	written += sizeof(UdpMsgType);

	// Write chunk header
	memcpy(buffer + offset + written, &geomUpdate, sizeof(GeomUpdateHeader));
	written += sizeof(GeomUpdateHeader);

	// Write chunk payload
	memcpy(buffer + offset + written,
		reinterpret_cast<uint8_t*>(dataPtr) + dataSize * geomUpdate.start,
		payloadSize);
	written += payloadSize;

	// Update size in header
	reinterpret_cast<UdpHeader*>(buffer)->size += written;
	verbose("Packet size is now ", reinterpret_cast<UdpHeader*>(buffer)->size);

	return written;
}

/** Updates a PointLight's color and/or intensity. To keep it simple, all properties are always sent anyway. */
static std::size_t addPointLightUpdate(uint8_t* buffer,
	std::size_t bufsize,
	std::size_t offset,
	const shared::PointLight& pointLight)
{
	// assert(offset < bufsize);

	const std::size_t payloadSize = sizeof(glm::vec3) + sizeof(float);

	// Prevent infinite loops
	assert(sizeof(UdpMsgType) + sizeof(PointLightUpdateHeader) + payloadSize < bufsize);

	if (offset + sizeof(UdpMsgType) + sizeof(PointLightUpdateHeader) + payloadSize > bufsize) {
		verbose("Not enough room!");
		return 0;
	}

	std::size_t written = 0;

	// Write chunk type
	static_assert(sizeof(UdpMsgType) == 1, "Need to change this code!");
	buffer[offset] = udpmsg2byte(UdpMsgType::POINT_LIGHT_UPDATE);
	written += sizeof(UdpMsgType);

	// Write header
	PointLightUpdateHeader header;
	header.lightId = pointLight.name;
	header.color = pointLight.color;
	header.intensity = pointLight.intensity;

	memcpy(buffer + offset + written, &header, sizeof(PointLightUpdateHeader));
	written += sizeof(PointLightUpdateHeader);

	// Update size in header
	reinterpret_cast<UdpHeader*>(buffer)->size += written;
	verbose("Packet size is now ", reinterpret_cast<UdpHeader*>(buffer)->size);

	dumpFullPacket(buffer, bufsize, LOGLV_VERBOSE);

	return written;
}

static std::size_t addTransformUpdate(uint8_t* buffer, std::size_t bufsize, std::size_t offset, const Node& node)
{
	// assert(offset < bufsize);

	// Header-only type of chunk
	const std::size_t payloadSize = 0;

	// Prevent infinite loops
	assert(sizeof(UdpMsgType) + sizeof(TransformUpdateHeader) + payloadSize < bufsize);

	if (offset + sizeof(UdpMsgType) + sizeof(TransformUpdateHeader) + payloadSize > bufsize) {
		verbose("Not enough room!");
		return 0;
	}

	std::size_t written = 0;

	// Write chunk type
	static_assert(sizeof(UdpMsgType) == 1, "Need to change this code!");
	buffer[offset] = udpmsg2byte(UdpMsgType::TRANSFORM_UPDATE);
	written += sizeof(UdpMsgType);

	// Write header
	TransformUpdateHeader header;
	header.objectId = node.name;
	header.transform = transformMatrix(node.transform);

	memcpy(buffer + offset + written, &header, sizeof(TransformUpdateHeader));
	written += sizeof(TransformUpdateHeader);

	// Update size in header
	reinterpret_cast<UdpHeader*>(buffer)->size += written;
	verbose("Packet size is now ", reinterpret_cast<UdpHeader*>(buffer)->size);

	dumpFullPacket(buffer, bufsize, LOGLV_DEBUG);

	return written;
}

std::size_t addUpdate(uint8_t* buffer,
	std::size_t bufsize,
	std::size_t offset,
	const QueuedUpdate& update,
	const Server& server)
{
	switch (update.type) {
		using T = QueuedUpdate::Type;
	case T::GEOM:
		return addGeomUpdate(buffer, bufsize, offset, update.data.geom.data, server.resources);

	case T::POINT_LIGHT: {
		const auto lightId = update.data.pointLight.lightId;
		const auto it = std::find_if(server.resources.pointLights.begin(),
			server.resources.pointLights.end(),
			[lightId](const auto& light) { return light.name == lightId; });
		if (it == server.resources.pointLights.end()) {
			throw std::runtime_error("addUpdate: tried to send update for inexisting point light " +
						 std::to_string(lightId) + "!");
		}
		return addPointLightUpdate(buffer, bufsize, offset, *it);
	}

	case T::TRANSFORM: {
		const auto objId = update.data.transform.objectId;
		const auto node = server.scene.getNode(objId);
		if (!node) {
			throw std::runtime_error(
				"addUpdate: tried to send update for inexisting object " + std::to_string(objId) + "!");
		}
		return addTransformUpdate(buffer, bufsize, offset, *node);
	}

	default:
		break;
	}

	err("Unknown QueuedUpdate type: " + std::to_string(int(update.type)));
	throw;
}

void dumpFullPacket(const uint8_t* buffer, std::size_t bufsize, LogLevel loglv)
{
	const auto header = reinterpret_cast<const UdpHeader*>(buffer);
	log(loglv, true, "header.packetGen:");
	dumpBytes(&header->packetGen, sizeof(uint64_t), 50, loglv);
	log(loglv, true, "header.size:");
	dumpBytes(&header->size, sizeof(uint32_t), 50, loglv);

	const auto type = byte2udpmsg(buffer[sizeof(UdpHeader)]);
	log(loglv, true, "chunk type: 0x", std::hex, int(buffer[sizeof(UdpHeader)]), std::dec, "  (", type, ")");

	switch (type) {
	case UdpMsgType::GEOM_UPDATE: {
		const auto chunkHead =
			reinterpret_cast<const GeomUpdateHeader*>(buffer + sizeof(UdpHeader) + sizeof(UdpMsgType));
		log(loglv, true, "chunkHead.modelId:");
		dumpBytes(&chunkHead->modelId, sizeof(uint32_t), 50, loglv);
		log(loglv, true, "chunkHead.dataType:");
		dumpBytes(&chunkHead->dataType, sizeof(GeomDataType), 50, loglv);
		log(loglv, true, "chunkHead.start:");
		dumpBytes(&chunkHead->start, sizeof(uint32_t), 50, loglv);
		log(loglv, true, "chunkHead.len:");
		dumpBytes(&chunkHead->len, sizeof(uint32_t), 50, loglv);
		log(loglv, true, "payload:");
		dumpBytes(buffer + sizeof(UdpHeader) + sizeof(GeomUpdateHeader), bufsize, 100, loglv);
	} break;
	case UdpMsgType::POINT_LIGHT_UPDATE: {
		const auto chunkHead = reinterpret_cast<const PointLightUpdateHeader*>(
			buffer + sizeof(UdpHeader) + sizeof(UdpMsgType));
		log(loglv, true, "chunkHead.lightId:");
		dumpBytes(&chunkHead->lightId, sizeof(uint32_t), 50, loglv);
		log(loglv, true, "chunkHead.color:");
		dumpBytes(&chunkHead->color, sizeof(glm::vec3), 50, loglv);
		log(loglv, true, "chunkHead.intensity:");
		dumpBytes(&chunkHead->intensity, sizeof(float), 50, loglv);
	} break;
	default:
		break;
	}
}

