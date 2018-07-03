#pragma once

#include "hashing.hpp"
#include "udp_messages.hpp"

struct QueuedUpdateGeom {
	GeomUpdateHeader data;
};

struct QueuedUpdatePointLight {
	// Only need to save which light changed
	StringId lightId;
};

struct QueuedUpdateTransform {
	// Only need to save which object changed
	StringId objectId;
};

/** A generic container for a queued update, used by the server to
 *  keep track of all the changes it must push to the client.
 *  These structs gather the minumum amount of data needed to build the actual
 *  packets that will be sent.
 *  Each QueuedUpdate will transformed into a single udp Chunk by the server active EP.
 *  It's the server counterpart of client's UpdateReq.
 */
struct QueuedUpdate {
	enum class Type { UNKNOWN, GEOM, POINT_LIGHT, TRANSFORM } type = Type::UNKNOWN;

	union {
		QueuedUpdateGeom geom;
		QueuedUpdatePointLight pointLight;
		QueuedUpdateTransform transform;
	} data;
};

inline QueuedUpdate newQueuedUpdateGeom(const GeomUpdateHeader& data) {
	QueuedUpdate up;
	up.type = QueuedUpdate::Type::GEOM;
	up.data.geom.data = data;
	return up;
}

inline QueuedUpdate newQueuedUpdatePointLight(StringId lightId) {
	QueuedUpdate up;
	up.type = QueuedUpdate::Type::POINT_LIGHT;
	up.data.pointLight.lightId = lightId;
	return up;
}

inline QueuedUpdate newQueuedUpdateTransform(StringId objId) {
	QueuedUpdate up;
	up.type = QueuedUpdate::Type::TRANSFORM;
	up.data.transform.objectId = objId;
	return up;
}