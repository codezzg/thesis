#include "model.hpp"
#include <unordered_map>
#include <chrono>
#include <iostream>
#include "xplatform.hpp"
#include "logging.hpp"
#define TINYOBJLOADER_IMPLEMENTATION
#include "third_party/tiny_obj_loader.h"

using namespace logging;
using shared::Mesh;

namespace to = tinyobj;

static Material saveMaterial(const char *modelPath, const to::material_t& mat);

Model loadModel(const char *modelPath, void *buffer) {

	Model model = {};

	to::attrib_t attrib;
	std::vector<to::shape_t> shapes;
	std::vector<to::material_t> materials;
	std::string err;

	const auto load_t_begin = std::chrono::high_resolution_clock::now();

	warn("Using STABLE tinyobj_loader");
	if (!to::LoadObj(&attrib, &shapes, &materials, &err, modelPath,
		(xplatDirname(modelPath) + "/").c_str(), // mtl base path
		true))                                   // triangulate
	{
		logging::err(err);
		return model;
	}
	const auto load_t_end = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double, std::milli> load_ms = load_t_end - load_t_begin;
	info("load time: ", load_ms.count(), " ms");

	std::unordered_map<Vertex, uint32_t> uniqueVertices;
	std::vector<Index> indices;

	model.meshes.reserve(shapes.size());
	model.nIndices = 0;
	for (const auto& shape : shapes) {
		Mesh mesh = {};

		// Material (XXX: for now we only use the first material)
		if (shape.mesh.material_ids.size() > 0)
			mesh.materialId = shape.mesh.material_ids[0];

		mesh.offset = indices.size();
		for (const auto& index : shape.mesh.indices) {
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
			if (index.texcoord_index >= 0) {
				vertex.texCoord = {
					attrib.texcoords[2 * index.texcoord_index + 0],
					1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
				};
			} else {
				vertex.texCoord = {};
			}

			if (uniqueVertices.count(vertex) == 0) {
				// This vertex is new: insert new index
				uniqueVertices[vertex] = model.nVertices;
				*(reinterpret_cast<Vertex*>(buffer) + model.nVertices) = vertex;
				model.nVertices++;
			}

			indices.emplace_back(uniqueVertices[vertex]);
		}

		mesh.len = indices.size() - mesh.offset;
		model.meshes.emplace_back(mesh);
	}

	model.vertices = reinterpret_cast<Vertex*>(buffer);
	model.indices = reinterpret_cast<Index*>(reinterpret_cast<uint8_t*>(buffer)
					+ sizeof(Vertex) * model.nVertices);
	model.nIndices = indices.size();

	// Save material info
	model.materials.reserve(materials.size());
	for (const auto& m : materials)
		model.materials.emplace_back(saveMaterial(modelPath, m));

	// Copy indices into buffer
	memcpy(model.indices, indices.data(), sizeof(Index) * indices.size());

	info("Model loaded: ", modelPath);
	info(model.toString());

	return model;
}

Material saveMaterial(const char *modelPath, const to::material_t& mat) {
	const std::string basePath = xplatDirname(modelPath) + DIRSEP;

	debug("material base path: ", basePath);

	Material material;
	material.name = sid(mat.name);
	if (mat.diffuse_texname.length() > 0) {
		const auto tex = basePath + mat.diffuse_texname;
		material.diffuseTex = tex;
	}

	if (mat.specular_texname.length() > 0) {
		const auto tex = basePath + mat.specular_texname;
		material.specularTex = tex;
	}

	return material;
}
