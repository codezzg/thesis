#include "server_appstage.hpp"
#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include "serialization.hpp"
#include <cmath>
#include <vector>
#include <unordered_map>

struct Sphere {
	glm::vec3 center;
	float radius;
};

static void wiggle(Model& model, const Camera& camera) {
	static float t = 0;
	for (int i = 0; i < model.nVertices; ++i) {
		model.vertices[i].pos += std::cos(t * 10 + i * 0.01);
		model.vertices[i].color = glm::normalize(camera.position);
	}
	t += 0.033;
}

static Sphere calcBoundingSphere(const Model& model) {
	// Using Ritter's algorithm

	// Pick a point x from the set of vertices
	const auto& x = model.vertices[0].pos;

	// Find y with the largest distance from x

	const auto findMaxDist = [&model] (const glm::vec3& x) {
		float d = 0;
		glm::vec3 y;
		for (int i = 0; i < model.nVertices; ++i) {
			const auto& v = model.vertices[i];
			if (v.pos == x) continue;
			const auto nd = glm::distance(x, v.pos);
			if (nd > d) {
				d = nd;
				y = v.pos;
			}
		}
		return y;
	};

	auto y = findMaxDist(x);

	// Search z with largest distance from y
	auto z = findMaxDist(y);

	// Build a ball with center = mid(y, z) and radius dist(y, z)
	auto center = (z + y) / 2.f;
	float radius = glm::distance(y, z) / 2.f;

	// Ensure all points are within ball
	for (int i = 0; i < model.nVertices; ++i) {
		const auto& v = model.vertices[i];
		const auto diff = glm::distance(v.pos, center) - radius;
		if (diff > 0) {
			radius += diff;
		}
	}

	return Sphere { center, radius };
}

void transformVertices(Model& model, const std::array<uint8_t, FrameData().payload.size()>& clientData,
		uint8_t *buffer, int& nVertices, int& nIndices) {
	const auto camera = deserializeCamera(clientData.data());

	// STUB
	//wiggle(model, camera);

	const auto& frustum = calcFrustum(camera.projMatrix());
	// TODO ugly af caching, works as long as we have only 1 model
	static const auto sphere = calcBoundingSphere(model);

	//std::cerr << "bounding sphere = " << sphere.center << ", r = " << sphere.radius << "\n";
	//std::cerr << "frustum = " << frustum.left << ", " << frustum.right << ", " <<
		//frustum.bottom << ", " << frustum.top << ", " << frustum.near << ", " << frustum.far << "\n";

	std::unordered_map<Index, Index> indexRemap;
	{
		// Filter model vertices and copy the good ones to the temp memory area (i.e. `buffer`)
		int vertexIdx = 0;
		auto verticesBuffer = reinterpret_cast<Vertex*>(buffer);
		for (int i = 0; i < model.nVertices; ++i) {
			const auto& v = model.vertices[i];
			const auto vv = camera.viewMatrix() * glm::vec4{v.pos.x, v.pos.y, v.pos.z, 1.0};
			if (true || sphereInFrustum(vv, sphere.radius * 2, frustum)) {
				verticesBuffer[vertexIdx] = v;
				indexRemap[i] = vertexIdx;
				if (!sphereInFrustum(vv, sphere.radius, frustum))
					verticesBuffer[vertexIdx].color = glm::vec3{ 1.f, 0.f, 0.f };
				++vertexIdx;
			}
		}
		nVertices = vertexIdx;
	}

	{
		// Remap all indices
		int indexIdx = 0;
		auto indicesBuffer = reinterpret_cast<Index*>(buffer + nVertices * sizeof(Vertex));
		for (int i = 0; i < model.nIndices; ++i) {
			const auto index = model.indices[i];
			const auto it = indexRemap.find(index);
			if (it == indexRemap.end()) {
				// Drop the index, it's not used anymore
				continue;
			}
			indicesBuffer[indexIdx] = it->second;
			++indexIdx;
		}
		nIndices = indexIdx;
	}
}