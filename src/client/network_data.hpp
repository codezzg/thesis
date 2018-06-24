#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class ClientPassiveEndpoint;
struct Geometry;
struct NetworkResources;

/** Receives network data from `passiveEP`, storing them into the staging buffer `buffer`.
 *  Then interprets the chunks received and updates `geometry` and `netRsrc` accordingly.
 */
void receiveData(ClientPassiveEndpoint& passiveEP,
	std::vector<uint8_t>& buffer,
	Geometry& geometry,
	NetworkResources& netRsrc);
