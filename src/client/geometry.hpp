#pragma once

#include "buffers.hpp"
#include "hashing.hpp"
#include "models.hpp"
#include <unordered_map>
#include <vulkan/vulkan.h>

struct Geometry {
	/** Single buffer containing all vertices for all models */
	Buffer vertexBuffer;

	/** Single buffer containing all indices for all models */
	Buffer indexBuffer;

	struct Location {
		VkDeviceSize vertexOff;
		VkDeviceSize indexOff;
		// No need to save vertexLen/indexLen, as this information is held in the models themselves.
	};

	/** Maps modelName => location into buffers */
	std::unordered_map<StringId, Location> locations;
};

/** Adds given vertex and index buffers to `bufAllocator`, calculating the proper sizes,
 *  and returns the locations designated to contain the geometry of `models`
 *  Note that the buffers are not actually created until bufAllocator.create() is called.
 */
auto addVertexAndIndexBuffers(BufferAllocator& bufAllocator,
	Buffer& vertexBuffer,
	Buffer& indexBuffer,
	const std::unordered_map<StringId, ModelInfo>& models) -> std::unordered_map<StringId, Geometry::Location>;
