#pragma once

#include "endpoint.hpp"
#include "shared_resources.hpp"
#include <string>

struct Material;
struct Model;
struct ServerResources;

bool sendMaterial(socket_t clientSocket, const Material& material);
bool sendPointLight(socket_t clientSocket, const shared::PointLight& light);

bool sendModel(socket_t clientSocket, const Model& model);

/** Sends a single texture via `clientSocket`.
 *  The first packet sent contains a header with the metadata and the beginning of the
 *  actual texture data.
 *  Then, if the complete data doesn't fit one packet, more packets are sent until all
 *  bytes are sent. These extra packets have no header.
 */
bool sendTexture(socket_t clientSocket,
	ServerResources& resources,
	const std::string& texName,
	shared::TextureFormat format);
