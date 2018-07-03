#include "model.hpp"
#include "cf_hashmap.hpp"
#include "defer.hpp"
#include "logging.hpp"
#include "profile.hpp"
#include "xplatform.hpp"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <chrono>
#include <iostream>

using namespace logging;
using shared::Mesh;

#define AICHECK(expr)                                                                                                 \
	do {                                                                                                          \
		const auto ret = expr;                                                                                \
		switch (ret) {                                                                                        \
		case aiReturn_FAILURE:                                                                                \
			throw std::runtime_error(                                                                     \
				std::string("Assimp failure at ") + __FILE__ + ":" + std::to_string(__LINE__));       \
		case aiReturn_OUTOFMEMORY:                                                                            \
			throw std::runtime_error(                                                                     \
				std::string("Assimp out of memory at ") + __FILE__ + ":" + std::to_string(__LINE__)); \
		default:                                                                                              \
			break;                                                                                        \
		}                                                                                                     \
	} while (false)

static Material saveMaterial(const char* modelPath, const aiMaterial* mat);

Model loadModel(const char* modelPath, void* buffer, std::size_t bufsize)
{
	const auto modelPathBase = xplatBasename(modelPath);
	Model model = {};

	Assimp::Importer importer;
	const aiScene* scene;

	measure_ms((std::string{ "Load model " } + modelPathBase).c_str(), LOGLV_INFO, [&]() {
		scene = importer.ReadFile(modelPath,
			aiProcess_PreTransformVertices | aiProcess_Triangulate | aiProcess_CalcTangentSpace |
				aiProcess_ImproveCacheLocality);
	});

	if (!scene) {
		err(importer.GetErrorString());
		return model;
	}

	// Use a map big enough to contain all vertices in the scene
	uint64_t nTotVertices = 0;
	for (unsigned i = 0; i < scene->mNumMeshes; ++i)
		nTotVertices += scene->mMeshes[i]->mNumVertices;
	const auto uniqueVerticesSize = CF_HASHMAP_GET_BUFFER_SIZE(Vertex, uint32_t, nTotVertices);
	debug("Allocating ", uniqueVerticesSize, " bytes for uniqueVertices hashmap");
	void* uniqueVerticesMem = malloc(uniqueVerticesSize);
	DEFER([uniqueVerticesMem]() { free(uniqueVerticesMem); });

	auto uniqueVertices = cf::hashmap<Vertex, uint32_t>::create(uniqueVerticesSize, uniqueVerticesMem);
	std::vector<Index> indices;

	model.meshes.reserve(scene->mNumMeshes);
	model.nIndices = 0;
	START_PROFILE(process);
	for (unsigned i = 0; i < scene->mNumMeshes; ++i) {

		auto shape = scene->mMeshes[i];

		Mesh mesh = {};

		// Material
		mesh.materialId = shape->mMaterialIndex;

		mesh.offset = indices.size();
		for (unsigned j = 0; j < shape->mNumVertices; ++j) {
			Vertex vertex = {};
			const auto v = shape->mVertices[j];
			vertex.pos = {
				v.x,
				v.y,
				v.z,
			};
			if (shape->HasNormals()) {
				const auto n = shape->mNormals[j];
				vertex.norm = {
					n.x,
					n.y,
					n.z,
				};
			} else {
				vertex.norm = {};
			}
			if (shape->HasTextureCoords(0)) {
				const auto t = shape->mTextureCoords[0][j];
				vertex.texCoord = {
					t.x,
					1.f - t.y,
				};
			} else {
				vertex.texCoord = {};
			}
			if (shape->HasTangentsAndBitangents()) {
				const auto t = shape->mTangents[j];
				const auto b = shape->mBitangents[j];
				vertex.tangent = {
					t.x,
					t.y,
					t.z,
				};
				vertex.bitangent = {
					b.x,
					b.y,
					b.z,
				};
			} else {
				vertex.tangent = {};
				vertex.bitangent = {};
			}

			uint32_t val;
			const auto h = std::hash<Vertex>{}(vertex);
			if (!uniqueVertices.lookup(h, vertex, val)) {
				// This vertex is new: insert new index
				val = model.nVertices;
				uniqueVertices.set(h, vertex, val);
				if (sizeof(Vertex) * model.nVertices >= bufsize) {
					err("loadModel(", modelPath, "): out of memory!");
					return model;
				}
				reinterpret_cast<Vertex*>(buffer)[model.nVertices] = vertex;
				model.nVertices++;
			}

			indices.emplace_back(val);
		}

		mesh.len = indices.size() - mesh.offset;
		model.meshes.emplace_back(mesh);
	}

	if (sizeof(Vertex) * model.nVertices + sizeof(Index) * indices.size() >= bufsize) {
		err("loadModel(", modelPath, "): out of memory!");
		return model;
	}

	model.name = sid(modelPath);
	model.vertices = reinterpret_cast<Vertex*>(buffer);
	model.indices = reinterpret_cast<Index*>(reinterpret_cast<uint8_t*>(buffer) + sizeof(Vertex) * model.nVertices);
	model.nIndices = indices.size();

	// Save material info
	model.materials.reserve(scene->mNumMaterials);
	for (unsigned i = 0; i < scene->mNumMaterials; ++i)
		model.materials.emplace_back(saveMaterial(modelPath, scene->mMaterials[i]));

	// Copy indices into buffer
	memcpy(model.indices, indices.data(), sizeof(Index) * indices.size());

	END_PROFILE(process, (std::string{ "Process model " } + modelPathBase).c_str(), LOGLV_INFO);

	debug(model.toString());
	debug("max idx = ", *std::max_element(indices.begin(), indices.end()));

	info("Loaded model ", modelPathBase, " (", model.name, ")");

	return model;
}

Material saveMaterial(const char* modelPath, const aiMaterial* mat)
{
	const std::string basePath = xplatDirname(modelPath) + DIRSEP;

	debug("material base path: ", basePath);

	Material material;
	{
		aiString name;
		AICHECK(mat->Get(AI_MATKEY_NAME, name));
		material.name = sid(name.C_Str());
	}

	if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
		aiString path;
		AICHECK(mat->GetTexture(aiTextureType_DIFFUSE, 0, &path));
		material.diffuseTex = basePath + path.C_Str();
	}
	if (mat->GetTextureCount(aiTextureType_SPECULAR) > 0) {
		aiString path;
		AICHECK(mat->GetTexture(aiTextureType_SPECULAR, 0, &path));
		material.specularTex = basePath + path.C_Str();
	}
	if (mat->GetTextureCount(aiTextureType_HEIGHT) > 0) {
		aiString path;
		AICHECK(mat->GetTexture(aiTextureType_HEIGHT, 0, &path));
		material.normalTex = basePath + path.C_Str();
	} else if (mat->GetTextureCount(aiTextureType_NORMALS) > 0) {
		aiString path;
		AICHECK(mat->GetTexture(aiTextureType_NORMALS, 0, &path));
		material.normalTex = basePath + path.C_Str();
	}

	return material;
}
