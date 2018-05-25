#pragma once

#include "hashing.hpp"
#include "tcp_messages.hpp"

/** The common data shared via network.
 *  The format of this data is valid for both the server and the client.
 */

namespace shared {

enum class TextureFormat : uint8_t {
	RGBA,
	GREY,
	UNKNOWN
};

/** Texture information. Note that this structure is only used to *store*
 *  that information, and is NOT sent directly
 *  via network (thus it's not packed); that's because texture data is sent
 *  as network data via TextureHeader + raw payload, possibly split into multiple packets.
 */
struct Texture {
	uint64_t size = 0;
	void *data = nullptr;
	TextureFormat format;
};

// The following structures are all sent directly through the network
#pragma pack(push, 1)
template <typename ResType>
struct ResourceHeader {
	MsgType type;
	uint64_t size;
	ResType head;
};

struct TextureHeader {
	StringId name;
	TextureFormat format;
};

struct Material {
	StringId name;
	StringId diffuseTex;
	StringId specularTex;
};
#pragma pack(pop)

} // end namespace shared
