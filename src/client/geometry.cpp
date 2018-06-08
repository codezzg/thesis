#include "geometry.hpp"
#include "logging.hpp"
#include "vertex.hpp"

using namespace logging;

auto addVertexAndIndexBuffers(BufferAllocator& bufAllocator,
	Buffer& vertexBuffer,
	Buffer& indexBuffer,
	const std::unordered_map<StringId, ModelInfo>& models) -> std::unordered_map<StringId, Geometry::Location>
{
	std::unordered_map<StringId, Geometry::Location> locations;
	VkDeviceSize vbufSize = 0, ibufSize = 0;

	for (const auto& pair : models) {
		const auto& model = pair.second;
		auto& loc = locations[pair.first];
		loc.vertexOff = vbufSize;
		loc.indexOff = ibufSize;
		vbufSize += model.nVertices * sizeof(Vertex);
		ibufSize += model.nIndices * sizeof(Index);
	}

	// vertex buffer
	bufAllocator.addBuffer(vertexBuffer,
		vbufSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	// index buffer
	bufAllocator.addBuffer(indexBuffer,
		ibufSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	info("Created vertexBuffer of size ",
		vbufSize / 1024,
		" KiB and indexBuffer of size ",
		ibufSize / 1024,
		" KiB.");
	info("Locations:");
	for (const auto& pair : locations) {
		const auto& loc = pair.second;
		info(pair.first, " => { voff: ", loc.vertexOff, ", ioff: ", loc.indexOff, " }");
	}

	return locations;
}
