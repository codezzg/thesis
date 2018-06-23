#pragma once

#include "hashing.hpp"
#include "tcp_messages.hpp"
#include <glm/glm.hpp>

/**
 *  The common data structure whose format is valid for both the server and the client.
 */

namespace shared {

enum class TextureFormat : uint8_t { RGBA, GREY, UNKNOWN };

/** Used to map parameters to bit in lights' `dynMask` */
enum class LightDynFlags : uint8_t {
	POSITION = 0,
	COLOR = 1,
	INTENSITY = 2,
};

template <typename L>
constexpr bool isLightPositionFixed(const L& light)
{
	return light.dynMask & (1 << static_cast<uint8_t>(LightDynFlags::POSITION));
}
template <typename L>
constexpr bool isLightColorFixed(const L& light)
{
	return light.dynMask & (1 << static_cast<uint8_t>(LightDynFlags::COLOR));
}
template <typename L>
constexpr bool isLightIntensityFixed(const L& light)
{
	return light.dynMask & (1 << static_cast<uint8_t>(LightDynFlags::INTENSITY));
}

/** Texture information. Note that this structure is only used to *store*
 *  that information, and is NOT sent directly
 *  via network (thus it's not packed); that's because texture data is sent
 *  as network data via TextureHeader + raw payload, possibly split into multiple packets.
 */
struct Texture {
	/** Size of `data` in bytes */
	uint64_t size = 0;
	/** Raw pixel data */
	void* data = nullptr;
	/** Format to use when creating a Vulkan texture out of this data */
	TextureFormat format;
};

/** Stores a PointLight's data. This struct is not sent via network:
 *  light initial data are sent with PointLightInfo, and updates are sent as UDP packets.
 */
struct PointLight {
	glm::vec3 position{ 0.f, 0.f, 0.f };
	glm::vec3 color{ 1.f, 1.f, 1.f };
	float intensity = 1;
	StringId name;
	uint8_t dynMask = 0;
};

// The following structures are all sent directly through the network
#pragma pack(push, 1)

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

/** A PointLight with a position, color and intensity.
 *  All those parameters can either be fixed or dynamic.
 */
struct PointLightInfo {
	StringId name;

	/** Initial position values (also final ones if position is fixed) */
	float x;
	float y;
	float z;

	/** Initial color values (also final ones if color is fixed) */
	float r;
	float g;
	float b;

	/** Initial intensity value (also final one if intensity is fixed) */
	float intensity;

	/** Bitmask representing fixed/dynamic state of this light's parameters.
	 *  0: fixed, 1: dynamic.
	 *  A fixed parameter can never be changed, which can enable some optimizations
	 *  both on server and client side.
	 */
	uint8_t dynMask;
};

struct Camera {
	float x;
	float y;
	float z;
	float yaw;
	float pitch;
};
#pragma pack(pop)

}   // end namespace shared
