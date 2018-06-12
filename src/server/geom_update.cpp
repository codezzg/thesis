#include "geom_update.hpp"
#include "logging.hpp"
#include "model.hpp"

using namespace logging;

// TODO: for now, we just update all vertices and indices
std::vector<udp::ChunkHeader> buildUpdatePackets(const Model& model)
{
	std::vector<udp::ChunkHeader> updates;

	// Figure out how many Chunks we need
	constexpr auto payloadSize = udp::UpdatePacket().payload.size();
	const auto maxVerticesPerPayload = (payloadSize - sizeof(udp::ChunkHeader)) / sizeof(Vertex);
	const auto maxIndicesPerPayload = (payloadSize - sizeof(udp::ChunkHeader)) / sizeof(Index);

	// XXX: did we get at least the order of magnitude right?
	updates.reserve(model.nVertices / maxVerticesPerPayload + model.nIndices / maxIndicesPerPayload + 2);

	unsigned i = 0;
	udp::ChunkHeader header;
	header.modelId = model.name;
	header.dataType = udp::DataType::VERTEX;
	// Shove in all the vertices
	while (i < model.nVertices) {
		header.start = i;
		header.len = std::min(
			static_cast<decltype(maxVerticesPerPayload)>(model.nVertices - i), maxVerticesPerPayload);
		updates.emplace_back(header);

		debug("Chunk size = ", sizeof(udp::ChunkHeader) + header.len * sizeof(Vertex));
		i += header.len;
	}

	header.dataType = udp::DataType::INDEX;
	// We're likely to have spare space in the last packet: fill it with indices if we can
	const auto spareBytes = i - i / maxVerticesPerPayload;
	if (spareBytes >= sizeof(udp::ChunkHeader) + sizeof(Index)) {
		header.start = 0;
		header.len = (spareBytes - sizeof(udp::ChunkHeader)) / sizeof(Index);
		updates.emplace_back(header);

		i = header.len;
	} else {
		i = 0;
	}

	// Now, send indices until we exhaust them
	while (i < model.nIndices) {
		header.start = i;
		header.len =
			std::min(static_cast<decltype(maxIndicesPerPayload)>(model.nIndices - i), maxIndicesPerPayload);
		updates.emplace_back(header);

		i += header.len;
	}

	info("Updates size for model ",
		model.name,
		": ",
		updates.size(),
		", guessed: ",
		model.nVertices / maxVerticesPerPayload + model.nIndices / maxIndicesPerPayload + 2);
	return updates;
}
