#pragma once

#include <cassert>
#include <unordered_map>
#include "model.hpp"
#include "hashing.hpp"

struct ServerResources final {

	/** This memory is owned externally */
	uint8_t *memory = nullptr;
	std::size_t memsize = 0;

	std::unordered_map<StringId, Model> models;

public:
	explicit ServerResources() {}

	/** `memory` is a pointer into a valid buffer. The buffer should be large enough to contain all
	 *  resources and should not be freed or otherwise manipulated by other than this class.
	 */
	void initialize(uint8_t *memory, std::size_t memsize) {
		this->memory = memory;
		this->memsize = memsize;
	}

	Model loadModel(const char *file) {
		assert(memsize > 0 && "ServerResources has no memory to use! Forgot to initialize it?");
		auto& model = models[sid(file)];
		model = ::loadModel(file, memory); // FIXME FIXME: will overwrite previous one
		return model;
	}
};
