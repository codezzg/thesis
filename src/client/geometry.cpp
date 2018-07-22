#include "geometry.hpp"
#include "application.hpp"
#include "commands.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include "vertex.hpp"
#include <algorithm>

using namespace logging;

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

static VkDeviceSize getNewSize(const Geometry& geometry, VkDeviceSize firstFreePos, ListType type, unsigned amtNeeded)
{
	const auto dataSize = type == ListType::VERTEX ? sizeof(Vertex) : sizeof(Index);
	const auto buf = type == ListType::VERTEX ? geometry.vertexBuffer : geometry.indexBuffer;
	const bool canAccomodate = (buf.size - firstFreePos) >= (dataSize * amtNeeded);

	VkDeviceSize newSize = 0;

	info("geometry: need ", dataSize * amtNeeded, ", have ", buf.size - firstFreePos);
	if (!canAccomodate) {
		// Grow exponentially
		newSize = 2 * (type == ListType::VERTEX ? geometry.vertexBuffer.size : geometry.indexBuffer.size);
		while (newSize - firstFreePos < dataSize * amtNeeded)
			newSize *= 2;
	}

	return newSize;
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

static void copyDataToNewBuffers(const Application& app, Buffer oldV, Buffer oldI, Buffer newV, Buffer newI)
{
	auto cmdBuf = beginSingleTimeCommands(app, app.commandPool);
	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = 0;
	copyRegion.dstOffset = 0;
	copyRegion.size = oldV.size;
	vkCmdCopyBuffer(cmdBuf, oldV.handle, newV.handle, 1, &copyRegion);

	copyRegion.size = oldI.size;
	vkCmdCopyBuffer(cmdBuf, oldI.handle, newI.handle, 1, &copyRegion);

	endSingleTimeCommands(app.device, app.queues.graphics, app.commandPool, cmdBuf);
}

static std::pair<Buffer, Buffer> createNewBuffers(const Application& app, VkDeviceSize vSize, VkDeviceSize iSize)
{
	BufferAllocator bufAllocator;
	Buffer newVertexBuffer, newIndexBuffer;

	bufAllocator.addBuffer(newVertexBuffer,
		vSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	bufAllocator.addBuffer(newIndexBuffer,
		iSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	bufAllocator.create(app);

	return std::make_pair(newVertexBuffer, newIndexBuffer);
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

	const auto freePos = getFirstFreePos(geometry);

	// Insert the new locations
	updateLocations(geometry, freePos.first, freePos.second, newModels);

	info("new locations: ", mapToString(geometry.locations, [](auto l) -> std::string {
		std::stringstream ss;
		ss << "{ voff: " << std::to_string(l.vertexOff) << ", vlen: " << std::to_string(l.vertexLen)
		   << ", ioff: " << std::to_string(l.indexOff) + ", ilen: " << std::to_string(l.indexLen) << " }";
		return ss.str();
	}));

	// Check if new vertices fit in existing buffer. If that's not the case, schedule a new
	// vertex and index buffer to be created. If even only one of them must be reallocated, we reallocate the
	// other too preventively.
	const auto newVSize = getNewSize(geometry, freePos.first, ListType::VERTEX, newVerticesNeeded);
	const auto newISize = getNewSize(geometry, freePos.second, ListType::INDEX, newIndicesNeeded);

	if (newVSize + newISize == 0)   // no buffer needs to be reallocated
		return;

	const std::vector<Buffer> buffersToDestroy = { geometry.vertexBuffer, geometry.indexBuffer };

	info("migrating geometry buffer(s). old = ", geometry.vertexBuffer.handle, " / ", geometry.indexBuffer.handle);

	const auto newBufs = createNewBuffers(
		app, std::max(geometry.vertexBuffer.size, newVSize), std::max(geometry.indexBuffer.size, newISize));

	// Copy data to new buffers
	copyDataToNewBuffers(app, geometry.vertexBuffer, geometry.indexBuffer, newBufs.first, newBufs.second);

	// Overwrite old buffers
	geometry.vertexBuffer = newBufs.first;
	geometry.indexBuffer = newBufs.second;

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
		" KiB; tot = ",
		(geometry.vertexBuffer.size + geometry.indexBuffer.size) / 1024 / 1024,
		" MiB)");

	// Map device memory to host for new buffers
	mapBuffersMemory(app.device,
		{
			&geometry.vertexBuffer,
			&geometry.indexBuffer,
		});
}

