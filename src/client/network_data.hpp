#pragma once

#include "client_resources.hpp"
#include "hashing.hpp"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>

class ClientPassiveEndpoint;
struct Geometry;
struct NetworkResources;

struct UpdateReqGeom {
	uint32_t serialId;

	/** Not strictly needed, but useful to keep here */
	StringId modelId;

	const void* src;
	void* dst;
	std::size_t nBytes;
};

struct UpdateReqPointLight {
	StringId lightId;
	glm::vec3 color;
	float intensity;
};

struct UpdateReqTransform {
	StringId objectId;
	glm::mat4 transform;
};

struct UpdateReq {
	enum class Type { UNKNOWN, GEOM, POINT_LIGHT, TRANSFORM } type = Type::UNKNOWN;

	union {
		UpdateReqGeom geom;
		UpdateReqPointLight pointLight;
		UpdateReqTransform transform;
	} data;
};

/** Receives network data from `passiveEP`, storing them into the staging buffer `buffer`.
 *  Then interprets the chunks received and fills `updateReqs` with all the updates that
 *  the server sent to us.
 */
void receiveData(ClientPassiveEndpoint& passiveEP,
	/* inout */ std::vector<uint8_t>& buffer,
	const Geometry& geometry,
	/* out */ std::vector<UpdateReq>& updateReqs,
	const std::unordered_set<uint32_t>& serialsToIgnore);

void updateModel(const UpdateReqGeom& req);
void updatePointLight(const UpdateReqPointLight& req, NetworkResources& netRsrc);
void updateTransform(const UpdateReqTransform& req, ObjectTransforms& transforms);
