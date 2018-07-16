#include "geometry.hpp"
#include "application.hpp"
#include "commands.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include "vertex.hpp"
#include <algorithm>

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

enum ListType { VERTEX, INDEX };

/** @returns the first free byte of vertexBuffer and indexBuffer of `geometry`. */
static std::pair<VkDeviceSize, VkDeviceSize> getFirstFreePos(const Geometry& geometry)
{
	VkDeviceSize vFirst = 0;
	VkDeviceSize iFirst = 0;

	for (auto loc : geometry.locations) {
		if (loc.second.vertexOff >= vFirst)
			vFirst = loc.second.vertexOff + loc.second.vertexLen;
		if (loc.second.indexOff >= iFirst)
			iFirst = loc.second.indexOff + loc.second.indexLen;
	}

	return std::make_pair(vFirst, iFirst);
}

static bool accomodateNewData(Geometry& geometry,
	BufferAllocator& bufAllocator,
	VkDeviceSize firstFreePos,
	ListType listType,
	unsigned amtNeeded,
	Buffer& newBuf)
{
	newBuf.handle = VK_NULL_HANDLE;

	const auto dataSize = listType == ListType::VERTEX ? sizeof(Vertex) : sizeof(Index);
	const auto buf = listType == ListType::VERTEX ? geometry.vertexBuffer : geometry.indexBuffer;
	bool canAccomodate = (buf.size - firstFreePos) >= (dataSize * amtNeeded);

	info("geometry: need ", dataSize * amtNeeded, ", have ", buf.size - firstFreePos);
	if (!canAccomodate) {
		// Grow exponentially
		auto newSize = 2 * geometry.vertexBuffer.size;
		while (newSize - firstFreePos < dataSize * amtNeeded)
			newSize *= 2;

		const auto typeFlag = listType == ListType::VERTEX ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
								   : VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

		bufAllocator.addBuffer(newBuf,
			newSize,
			typeFlag | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	return canAccomodate;
}

static void updateLocations(Geometry& geometry,
	VkDeviceSize vFirst,
	VkDeviceSize iFirst,
	const std::vector<ModelInfo>& newModels)
{
	VkDeviceSize nextOff = vFirst;
	for (const auto& model : newModels) {
		geometry.locations[model.name].vertexOff = nextOff;
		geometry.locations[model.name].vertexLen = model.nVertices * sizeof(Vertex);
		nextOff += model.nVertices * sizeof(Vertex);
	}
	nextOff = iFirst;
	for (const auto& model : newModels) {
		geometry.locations[model.name].indexOff = nextOff;
		geometry.locations[model.name].indexLen = model.nIndices * sizeof(Index);
		nextOff += model.nIndices * sizeof(Index);
	}
}

void updateGeometryBuffers(const Application& app, Geometry& geometry, const std::vector<ModelInfo>& newModels)
{
	// Check if we have enough room to accomodate new models without reallocating
	unsigned newVerticesNeeded = 0;
	unsigned newIndicesNeeded = 0;
	for (const auto& model : newModels) {
		newVerticesNeeded += model.nVertices;
		newIndicesNeeded += model.nIndices;
	}

	std::vector<Buffer> buffersToDestroy;
	BufferAllocator bufAllocator;

	// Check if new vertices fit in existing buffer. If that's not the case, schedule a new
	// vertex buffer to be created. Either way, add the new locations for vertices to geometry.
	Buffer newVertexBuffer, newIndexBuffer;
	const auto freePos = getFirstFreePos(geometry);
	bool canAccomodate = accomodateNewData(
		geometry, bufAllocator, freePos.first, ListType::VERTEX, newVerticesNeeded, newVertexBuffer);
	if (!canAccomodate)
		buffersToDestroy.emplace_back(geometry.vertexBuffer);

	canAccomodate = accomodateNewData(
		geometry, bufAllocator, freePos.second, ListType::INDEX, newIndicesNeeded, newIndexBuffer);
	if (!canAccomodate)
		buffersToDestroy.emplace_back(geometry.indexBuffer);

	updateLocations(geometry, freePos.first, freePos.second, newModels);

	info("new locations: ", mapToString(geometry.locations, [](auto l) -> std::string {
		std::stringstream ss;
		ss << "{ voff: " << std::to_string(l.vertexOff) << ", vlen: " << std::to_string(l.vertexLen)
		   << ", ioff: " << std::to_string(l.indexOff) + ", ilen: " << std::to_string(l.indexLen) << " }";
		return ss.str();
	}));

	if (buffersToDestroy.size() == 0)   // no buffer needs to be reallocated
		return;

	info("migrating geometry buffer(s). old = ", geometry.vertexBuffer.handle, " / ", geometry.indexBuffer.handle);

	// Copy data to new buffers
	bufAllocator.create(app);

	VkCommandBuffer cmdBuf = VK_NULL_HANDLE;

	assert(newVertexBuffer.handle != VK_NULL_HANDLE || newIndexBuffer.handle != VK_NULL_HANDLE);

	if (newVertexBuffer.handle != VK_NULL_HANDLE) {
		if (cmdBuf == VK_NULL_HANDLE)
			cmdBuf = beginSingleTimeCommands(app, app.commandPool);
		VkBufferCopy copyRegion = {};
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = 0;
		copyRegion.size = geometry.vertexBuffer.size;
		vkCmdCopyBuffer(cmdBuf, geometry.vertexBuffer.handle, newVertexBuffer.handle, 1, &copyRegion);

		// Reassign
		geometry.vertexBuffer = newVertexBuffer;
	}

	if (newIndexBuffer.handle != VK_NULL_HANDLE) {
		if (cmdBuf == VK_NULL_HANDLE)
			cmdBuf = beginSingleTimeCommands(app, app.commandPool);
		VkBufferCopy copyRegion = {};
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = 0;
		copyRegion.size = geometry.indexBuffer.size;
		vkCmdCopyBuffer(cmdBuf, geometry.indexBuffer.handle, newIndexBuffer.handle, 1, &copyRegion);

		geometry.indexBuffer = newIndexBuffer;
	}

	assert(cmdBuf != VK_NULL_HANDLE);
	endSingleTimeCommands(app.device, app.queues.graphics, app.commandPool, cmdBuf);

	// Destroy old buffers
	unmapBuffersMemory(app.device, buffersToDestroy);
	destroyAllBuffers(app.device, buffersToDestroy);

	info("new: ",
		geometry.vertexBuffer.handle,
		" / ",
		geometry.indexBuffer.handle,
		" (size = ",
		geometry.vertexBuffer.size / 1024,
		" KiB / ",
		geometry.indexBuffer.size / 1024,
		" KiB)");

	// Map device memory to host for new buffers
	mapBuffersMemory(app.device,
		{
			&geometry.vertexBuffer,
			&geometry.indexBuffer,
		});
}

