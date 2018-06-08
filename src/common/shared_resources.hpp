#pragma once

#include "hashing.hpp"
#include "tcp_messages.hpp"

/** The common data shared via network.
 *  The format of this data is valid for both the server and the client.
 */

namespace shared {

enum class TextureFormat : uint8_t { RGBA, GREY, UNKNOWN };

/** Texture information. Note that this structure is only used to *store*
 *  that information, and is NOT sent directly
 *  via network (thus it's not packed); that's because texture data is sent
 *  as network data via TextureHeader + raw payload, possibly split into multiple packets.
 */
struct Texture {
	uint64_t size = 0;
	void* data = nullptr;
	TextureFormat format;
};

// The following structures are all sent directly through the network
#pragma pack(push, 1)
/** ResType should be one of the structs below */
template <typename ResType>
struct ResourcePacket {
	MsgType type;
	ResType res;
};

struct TextureInfo {
	StringId name;
	TextureFormat format;
	uint64_t size;
	/** Follows payload: texture data */
};

struct Material {
	StringId name;
	StringId diffuseTex;
	StringId specularTex;
	StringId normalTex;
};

/** A Mesh represents a group of indices into the parent model.
 *  These indices all use the same material.
 */
struct Mesh {
	/** Offset into the parent model's indices */
	uint32_t offset;
	/** Amount of indices */
	uint32_t len;

	// TODO material per face?
	/** Index into parent model's materials. */
	int16_t materialId = -1;
};

struct Model {
	StringId name;
	uint32_t nVertices;
	uint32_t nIndices;
	uint8_t nMaterials;
	uint8_t nMeshes;
	/** Follows payload: [materialIds (StringId) | meshes (shared::Mesh)] */
};
#pragma pack(pop)

}   // end namespace shared
