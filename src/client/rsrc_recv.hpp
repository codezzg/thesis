#pragma once

#include "endpoint.hpp"

struct ClientTmpResources;

/** Reads header data from `buffer` and starts reading a texture. If more packets
 *  need to be read for the texture, receive them from `socket` until completion.
 *  Texture received is stored into `resources`.
 */
bool receiveTexture(socket_t socket,
	const uint8_t* buffer,
	std::size_t bufsize,
	/* out */ ClientTmpResources& resources);

/** Reads a material out of `buffer` and store it in `resources` */
bool receiveMaterial(const uint8_t* buffer,
	std::size_t bufsize,
	/* out */ ClientTmpResources& resources);

/** Reads header data out of `buffer` and starts reading model info. If more packets
 *  need to be read for the model info, receive them from `socket` until completion.
 *  Model received is stored into `resources`.
 */
bool receiveModel(socket_t socket,
	const uint8_t* buffer,
	std::size_t bufsize,
	/* out */ ClientTmpResources& resources);

bool receivePointLight(const uint8_t* buffer,
	std::size_t bufsize,
	/* out */ ClientTmpResources& resources);
