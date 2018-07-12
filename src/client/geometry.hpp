#pragma once

#include "buffers.hpp"
#include "hashing.hpp"
#include "models.hpp"
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

struct Application;

struct Geometry {
	/** Single buffer containing all vertices for all models */
	Buffer vertexBuffer;

	/** Single buffer containing all indices for all models */
	Buffer indexBuffer;

	/** Offsets in byte of the first vertex/index inside the buffers for each model */
	struct Location {
		VkDeviceSize vertexOff;
		VkDeviceSize vertexLen;
		VkDeviceSize indexOff;
		VkDeviceSize indexLen;
	};

	/** Maps modelName => location into buffers */
	std::unordered_map<StringId, Location> locations;
};

void updateGeometryBuffers(const Application& app, Geometry& geometry, const std::vector<ModelInfo>& models);
