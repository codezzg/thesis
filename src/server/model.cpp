#include "model.hpp"
#include <unordered_map>
#include <chrono>
#include <iostream>
#include "xplatform.hpp"
#include "logging.hpp"
#ifdef USE_EXPERIMENTAL_TINYOBJ
#	define TINYOBJ_LOADER_OPT_IMPLEMENTATION
#	include "third_party/tinyobj_loader_opt.h"
#	include <cstdlib>
#	include <fstream>
#	include <thread>
#else
#	define TINYOBJLOADER_IMPLEMENTATION
#	include "third_party/tiny_obj_loader.h"
#endif

using namespace logging;

#ifdef USE_EXPERIMENTAL_TINYOBJ
static char* mmap_file(const char *filename, std::size_t& len) {
	len = 0;
	{
		std::ifstream f { filename, std::ios::binary };
		if (!f) {
			err("Failed to open file: ", filename);
			return nullptr;
		}
		f.seekg(0, std::ios::end);
		len = f.tellg();
	}

	if (len < 16) {
		err("Empty or invalid .obj: ", filename);
		return nullptr;
	}

	// Thank you ifstream for not allowing to retreive the file descriptor
	auto fd = open(filename, O_RDONLY);
	if (fd == -1) {
		err("Error opening file: ", filename, ": ", std::strerror(errno), " (", errno, ")");
		return nullptr;
	}

	struct stat sb;
	if (fstat(fd, &sb) == -1) {
		err("Error stat'ing file: ", filename, ": ", std::strerror(errno), " (", errno, ")");
		return nullptr;
	}

	if (!S_ISREG(sb.st_mode)) {
		err(filename, " is not a file.");
		return nullptr;
	}

	auto p = reinterpret_cast<char*>(mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0));

	if (p == MAP_FAILED) {
		err("Error mmap'ing file: ", filename, ": ", std::strerror(errno), " (", errno, ")");
		close(fd);
		return nullptr;
	}

	if (close(fd) == -1) {
		err("Error closing file: ", filename, ": ", std::strerror(errno), " (", errno, ")");
		return nullptr;
	}

	return p;
}
#endif // USE_EXPERIMENTAL_TINYOBJ


Model loadModel(const char *modelPath, uint8_t *buffer) {

	Model model = {};

#ifdef USE_EXPERIMENTAL_TINYOBJ
	namespace to = tinyobj_opt;
#else
	namespace to = tinyobj;
#endif

	to::attrib_t attrib;
	std::vector<to::shape_t> shapes;
	std::vector<to::material_t> materials;
	std::string err;

	const auto load_t_begin = std::chrono::high_resolution_clock::now();
#ifdef USE_EXPERIMENTAL_TINYOBJ
	std::size_t dataLen = 0;
	char *data = mmap_file(modelPath, dataLen);
	if (data == nullptr) {
		warn("failed to load file\n");
		return model;
	}
	auto load_t_end = std::chrono::high_resolution_clock::now();
	const std::chrono::duration<double, std::milli> load_ms = load_t_end - load_t_begin;
	info("filesize: ", dataLen, " B");
	info("time to load into memory: ", load_ms.count(), " ms");

	tinyobj_opt::LoadOption option;
	option.req_num_threads = std::thread::hardware_concurrency();
	option.verbose = true;
	option.mtl_base_path = xplatDirname(modelPath);
	bool ret = to::parseObj(&attrib, &shapes, &materials, data, dataLen, option);
	munmap(data, dataLen);
	if (!ret) {
		warn("failed to load model!");
		return model;
	}
#else
	if (!to::LoadObj(&attrib, &shapes, &materials, &err, modelPath)) {
		logging::err(err);
		return model;
	}
	auto load_t_end = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double, std::milli> load_ms = load_t_end - load_t_begin;
	info("load time: ", load_ms.count(), " ms");
#endif

	std::unordered_map<Vertex, uint32_t> uniqueVertices;
	std::vector<Index> indices;

#ifdef USE_EXPERIMENTAL_TINYOBJ
	indices.reserve(attrib.indices.size());
	for (const auto& index : attrib.indices) {
#else
	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
#endif
			Vertex vertex = {};
			vertex.pos = {
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2],
			};
			if (index.normal_index >= 0) {
				vertex.norm = {
					attrib.normals[3 * index.normal_index + 0],
					attrib.normals[3 * index.normal_index + 1],
					attrib.normals[3 * index.normal_index + 2],
				};
			}
			else {
				vertex.norm = {};
			}
			vertex.texCoord = {
				attrib.texcoords[2 * index.texcoord_index + 0],
				1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
			};

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = model.nVertices;
				*(reinterpret_cast<Vertex*>(buffer) + model.nVertices) = vertex;
				model.nVertices++;
			}

			indices.emplace_back(uniqueVertices[vertex]);
#ifndef USE_EXPERIMENTAL_TINYOBJ
		}
#endif
	}

	info("textures used: (", materials.size(), ")");
	for (auto& m : materials)
		info(m.diffuse_texname);

	model.vertices = reinterpret_cast<Vertex*>(buffer);
	model.indices = reinterpret_cast<Index*>(buffer + sizeof(Vertex) * model.nVertices);
	model.nIndices = indices.size();

	// Copy indices into buffer
	memcpy(model.indices, indices.data(), sizeof(Index) * indices.size());

	info("vertices, indices = ", model.nVertices, ", ", model.nIndices);

	return model;
}
