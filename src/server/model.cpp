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
#	include "utils.hpp"
#else
#	define TINYOBJLOADER_IMPLEMENTATION
#	include "third_party/tiny_obj_loader.h"
#endif

using namespace logging;

#ifdef USE_EXPERIMENTAL_TINYOBJ
	namespace to = tinyobj_opt;
#else
	namespace to = tinyobj;
#endif

static Material saveMaterial(const char *modelPath, const to::material_t& mat, std::unordered_set<std::string>& outTextures);

Model loadModel(const char *modelPath, void *buffer, std::unordered_set<std::string>& outTextures) {

	Model model = {};

	to::attrib_t attrib;
	std::vector<to::shape_t> shapes;
	std::vector<to::material_t> materials;
	std::string err;

	const auto load_t_begin = std::chrono::high_resolution_clock::now();

#ifdef USE_EXPERIMENTAL_TINYOBJ
	auto data = readFileIntoMemory(modelPath);

	if (data.size() == 0) {
		warn("failed to load model ", modelPath);
		return model;
	}

	auto load_t_end = std::chrono::high_resolution_clock::now();
	const std::chrono::duration<double, std::milli> load_ms = load_t_end - load_t_begin;
	info("time to load into memory: ", load_ms.count(), " ms");

	tinyobj_opt::LoadOption option;
	option.req_num_threads = std::thread::hardware_concurrency();
	option.verbose = true;
	debug("dirname = ", xplatDirname(modelPath), " ( from ", modelPath, ")");
	option.mtl_base_path = xplatDirname(modelPath);
	bool ret = to::parseObj(&attrib, &shapes, &materials, data.data(), data.size(), option);
	if (!ret) {
		warn("failed to load model!");
		return model;
	}

	// Deallocate file from memory
	std::vector<char>().swap(data);

#else
	warn("Using STABLE tinyobj_loader");
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

	model.vertices = reinterpret_cast<Vertex*>(buffer);
	model.indices = reinterpret_cast<Index*>(reinterpret_cast<uint8_t*>(buffer)
					+ sizeof(Vertex) * model.nVertices);
	model.nIndices = indices.size();

	// Save material info
	model.materials.reserve(materials.size());
	info("materials used: (", materials.size(), ")");
	for (const auto& m : materials)
		model.materials.emplace_back(saveMaterial(modelPath, m, outTextures));

	// Copy indices into buffer
	memcpy(model.indices, indices.data(), sizeof(Index) * indices.size());

	info("vertices, indices = ", model.nVertices, ", ", model.nIndices);

	return model;
}

Material saveMaterial(const char *modelPath, const to::material_t& mat, std::unordered_set<std::string>& outTextures) {
	const std::string basePath = xplatDirname(modelPath) + DIRSEP;

	debug("material base path: ", basePath);

	Material material;
	material.name = sid(mat.name);
	if (mat.diffuse_texname.length() > 0) {
		const auto tex = basePath + mat.diffuse_texname;
		outTextures.emplace(tex);
		material.diffuseTex = sid(tex);
	}

	if (mat.specular_texname.length() > 0) {
		const auto tex = basePath + mat.specular_texname;
		outTextures.emplace(tex);
		material.specularTex = sid(tex);
	}

	return material;
}
