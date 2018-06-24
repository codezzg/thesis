#include "model.hpp"
#include "logging.hpp"
#include "xplatform.hpp"
#include <chrono>
#include <iostream>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

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
	Model model = {};

	const auto load_t_begin = std::chrono::high_resolution_clock::now();

	Assimp::Importer importer;
	auto scene = importer.ReadFile(modelPath,
		aiProcess_PreTransformVertices | aiProcess_Triangulate | aiProcess_CalcTangentSpace |
			aiProcess_ImproveCacheLocality);

	if (!scene) {
		err(importer.GetErrorString());
		return model;
	}

	const auto load_t_end = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double, std::milli> load_ms = load_t_end - load_t_begin;
	info("load time: ", load_ms.count(), " ms");

	std::unordered_map<Vertex, uint32_t> uniqueVertices;
	std::vector<Index> indices;

	model.meshes.reserve(scene->mNumMeshes);
	model.nIndices = 0;
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

			if (uniqueVertices.count(vertex) == 0) {
				// This vertex is new: insert new index
				uniqueVertices[vertex] = model.nVertices;
				if (sizeof(Vertex) * model.nVertices >= bufsize) {
					err("loadModel(", modelPath, "): out of memory!");
					return model;
				}
				reinterpret_cast<Vertex*>(buffer)[model.nVertices] = vertex;
				model.nVertices++;
			}

			indices.emplace_back(uniqueVertices[vertex]);
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

	info("Model loaded: ", modelPath);
	info(model.toString());

	info("max idx = ", *std::max_element(indices.begin(), indices.end()));

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
	}

	return material;
}
