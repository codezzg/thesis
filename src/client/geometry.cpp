#include "geometry.hpp"
#include "application.hpp"
#include "logging.hpp"
#include "vertex.hpp"

using namespace logging;

/** Adds given vertex and index buffers to `bufAllocator`, calculating the proper sizes,
 *  and returns the locations designated to contain the geometry of `models`
 *  Note that the buffers are not actually created until bufAllocator.create() is called.
 *  NOTE: models must contain at least a valid element.
 */
static auto addVertexAndIndexBuffers(BufferAllocator& bufAllocator,
	Buffer& vertexBuffer,
	Buffer& indexBuffer,
	const std::vector<ModelInfo>& models) -> std::unordered_map<StringId, Geometry::Location>
{
	std::unordered_map<StringId, Geometry::Location> locations;

	if (models.size() == 0)
		return locations;

	VkDeviceSize vbufSize = 0, ibufSize = 0;

	for (const auto& model : models) {
		auto& loc = locations[model.name];
		loc.vertexOff = vbufSize;
		loc.indexOff = ibufSize;
		vbufSize += model.nVertices * sizeof(Vertex);
		ibufSize += model.nIndices * sizeof(Index);
	}

	assert(vbufSize > 0 && ibufSize > 0);

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

void updateGeometryBuffers(const Application& app, Geometry& geometry, const std::vector<ModelInfo>& models)
{
	// Destroy current buffers if already existing
	if (geometry.locations.size() > 0) {
		destroyAllBuffers(app.device, { geometry.vertexBuffer, geometry.indexBuffer });
	}

	if (models.size() == 0)
		return;

	// Create vertex, and index buffers. These buffers are all recreated every time new models are sent.
	BufferAllocator bufAllocator;

	// schedule vertex/index buffer to be created and set the proper offsets into the common buffer for all models
	geometry.locations =
		addVertexAndIndexBuffers(bufAllocator, geometry.vertexBuffer, geometry.indexBuffer, models);

	bufAllocator.create(app);

	// Map device memory to host
	mapBuffersMemory(app.device,
		{
			&geometry.vertexBuffer,
			&geometry.indexBuffer,
		});
}

