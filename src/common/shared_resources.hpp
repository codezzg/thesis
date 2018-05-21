#pragma once

#include "hashing.hpp"

/** The common data shared via network.
 *  The format of this data is valid for both the server and the client.
 */

namespace shared {

enum class TextureFormat : uint8_t {
	RGBA,
	GREY
};

struct Texture {
	uint64_t size = 0;
	void *data = nullptr;
	TextureFormat format;
};

#pragma pack(push, 1)
struct TextureHeader {
	StringId name;
	TextureFormat format;
};
#pragma pack(pop)

} // end namespace shared
