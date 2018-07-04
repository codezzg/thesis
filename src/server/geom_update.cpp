#include "geom_update.hpp"
#include "logging.hpp"
#include "model.hpp"

using namespace logging;

// TODO: for now, we just update all vertices and indices
std::vector<GeomUpdateHeader> buildUpdatePackets(const Model& model)
{
	// start from 1, so we know 0 is invalid
	static uint32_t packetSerialId = 1;

	std::vector<GeomUpdateHeader> updates;

	// Figure out how many Chunks we need
	constexpr auto payloadSize = UdpPacket().payload.size();
	const auto maxVerticesPerPayload = (payloadSize - sizeof(GeomUpdateHeader)) / sizeof(Vertex);
	const auto maxIndicesPerPayload = (payloadSize - sizeof(GeomUpdateHeader)) / sizeof(Index);

	updates.reserve(model.nVertices / maxVerticesPerPayload + model.nIndices / maxIndicesPerPayload + 2);

	unsigned i = 0;
	GeomUpdateHeader header;
	header.modelId = model.name;
	header.dataType = GeomDataType::VERTEX;
	// Shove in all the vertices
	while (i < model.nVertices) {
		header.serialId = packetSerialId++;
		header.start = i;
		header.len = std::min(
			static_cast<decltype(maxVerticesPerPayload)>(model.nVertices - i), maxVerticesPerPayload);
		updates.emplace_back(header);

		i += header.len;
	}

	header.dataType = GeomDataType::INDEX;
	// We're likely to have spare space in the last packet: fill it with indices if we can
	const auto spareBytes = maxVerticesPerPayload - i + maxVerticesPerPayload * (i / maxVerticesPerPayload);
	if (spareBytes >= sizeof(GeomUpdateHeader) + sizeof(Index)) {
		header.serialId = packetSerialId++;
		header.start = 0;
		header.len = (spareBytes - sizeof(GeomUpdateHeader)) / sizeof(Index);
		updates.emplace_back(header);

		i = header.len;
	} else {
		i = 0;
	}

	// Now, send indices until we exhaust them
	while (i < model.nIndices) {
		header.serialId = packetSerialId++;
		header.start = i;
		header.len =
			std::min(static_cast<decltype(maxIndicesPerPayload)>(model.nIndices - i), maxIndicesPerPayload);
		updates.emplace_back(header);

		i += header.len;
	}

	verbose("Updates size for model ",
		model.name,
		": ",
		updates.size(),
		", guessed: ",
		model.nVertices / maxVerticesPerPayload + model.nIndices / maxIndicesPerPayload + 2);

	return updates;
}
