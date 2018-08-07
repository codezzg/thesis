#pragma once

#include "hashing.hpp"
#include "shared_resources.hpp"
#include "vertex.hpp"
#include <sstream>
#include <unordered_set>
#include <vector>

struct Material {
	StringId name = SID_NONE;

	std::string diffuseTex;
	std::string specularTex;
	std::string normalTex;
};

/** These data are stored outside the Model struct
 *  to avoid Model::operator= and similar to do a deep expensive copy
 *  of all these data.
 */
struct ModelColdData {
	std::vector<shared::Mesh> meshes;
	std::vector<Material> materials;
};

/* Model information.
 * A complete "model" consists in attrib data + attrib information
 * 1. Attrib DATA are the actual vertex data (position, normal, texcoords) and indices, and are
 *    stored contiguously in memory. These are the data that we send via UDP to the client.
 * 2. Attrib INFORMATION are the pointers into this data, and they're divided into
 *    several meshes, which may use different materials. This information is sent
 *    beforehand via TCP, so the client knows what the model's structure is.
 * This structure only stores the information, not the data itself.
 */
struct Model {

	StringId name = SID_NONE;

	/** Unowning pointer to the model's vertices */
	Vertex* vertices = nullptr;
	/** Unowning pointer to the model's indices */
	Index* indices = nullptr;
	/** Unowning pointer to the model's cold data */
	ModelColdData* data = nullptr;

	uint32_t nVertices = 0;
	uint32_t nIndices = 0;

	bool operator==(const Model& other) const { return name == other.name; }

	std::size_t size() const { return nVertices * sizeof(Vertex) + nIndices * sizeof(Index); }

	std::string toString() const
	{
		std::stringstream ss;
		ss << "n vertices = " << nVertices << ", n indices = " << nIndices << "\n"
		   << "size: " << size() << " bytes\n";
		if (data) {
			ss << "# materials: " << data->materials.size() << "\n";
			for (const auto& mat : data->materials) {
				ss << "mat { name = " << mat.name << ", diff = " << mat.diffuseTex
				   << ", spec = " << mat.specularTex << ", norm = " << mat.normalTex << " }\n";
			}
			ss << "# meshes: " << data->meshes.size() << "\n";
			for (const auto& mesh : data->meshes) {
				ss << "mesh { off = " << mesh.offset << ", len = " << mesh.len
				   << ", mat = " << mesh.materialId << " }\n";
			}
		}
		return ss.str();
	}
};

namespace std {
template <>
struct hash<Model> {
	std::size_t operator()(const Model& model) const { return model.name; }
};
}   // namespace std

/** Loads a model's vertices and indices into `buffer`.
 *  `buffer` and `coldData` must be pointers to initialized memory.
 *  Upon success, `buffer` gets filled with [vertices|indices] (indices start at
 *  offset `sizeof(Vertex) * nVertices`) and `coldData` is filled with a pointer to the model's cold data.
 *  @return a valid model, or one with nullptr `vertices` and `indices` if there were errors.
 */
Model loadModel(const char* modelPath,
	/* inout */ void* buffer,
	/* inout */ ModelColdData* coldData,
	std::size_t bufsize);
