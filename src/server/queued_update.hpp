#pragma once

#include "hashing.hpp"
#include "udp_messages.hpp"

/** A generic container for a queued update, used by the server to
 *  keep track of all the changes it must push to the client.
 *  These structs gather the minumum amount of data needed to build the actual
 *  packets that will be sent.
 *  Each QueuedUpdate will transformed into a single udp Chunk by the server active EP.
 *  To use it, it must be downcast to the type determined by `updateType`.
 */
struct QueuedUpdate {
	UdpMsgType updateType = UdpMsgType::UNKNOWN;

protected:
	QueuedUpdate() {}
};

struct QueuedUpdateGeom : public QueuedUpdate {
	explicit QueuedUpdateGeom(const GeomUpdateHeader& gdata)
		: QueuedUpdate()
	{
		updateType = UdpMsgType::GEOM_UPDATE;
		data = gdata;
	}

	GeomUpdateHeader data;
};

struct QueuedUpdatePointLight : public QueuedUpdate {
	explicit QueuedUpdatePointLight(StringId id)
		: QueuedUpdate()
	{
		updateType = UdpMsgType::POINT_LIGHT_UPDATE;
		lightId = id;
		assert(lightId != SID_NONE);
	}
	// Only need to save which light changed
	StringId lightId;
};

struct QueuedUpdateTransform : public QueuedUpdate {
	explicit QueuedUpdateTransform(StringId id)
		: QueuedUpdate()
	{
		updateType = UdpMsgType::TRANSFORM_UPDATE;
		objectId = id;
		assert(objectId != SID_NONE);
	}
	// Only need to save which object changed
	StringId objectId;
};
