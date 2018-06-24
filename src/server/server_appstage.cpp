#include "server_appstage.hpp"
#include "clock.hpp"
#include "frame_utils.hpp"
#include "geom_update.hpp"
#include "logging.hpp"
#include "server.hpp"
#include "to_string.hpp"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>
#include <iostream>
#include <random>
#include <unordered_map>
#include <vector>

using namespace logging;

struct Sphere {
	glm::vec3 center;
	float radius;
};

static void wiggle(Model& model)
{
	static float t = 0;
	for (unsigned i = 0; i < model.nVertices; ++i) {
		model.vertices[i].pos += 0.1 * std::cos(t * 10 + i * 0.01);
	}
	t += Clock::instance().deltaTime();
}

static Sphere calcBoundingSphere(const Model& model)
{
	// Using Ritter's algorithm

	// Pick a point x from the set of vertices
	const auto& x = model.vertices[0].pos;

	// Find y with the largest distance from x

	const auto findMaxDist = [&model](const glm::vec3& x) {
		float d = 0;
		glm::vec3 y;
		for (unsigned i = 0; i < model.nVertices; ++i) {
			const auto& v = model.vertices[i];
			if (v.pos == x)
				continue;
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
	for (unsigned i = 0; i < model.nVertices; ++i) {
		const auto& v = model.vertices[i];
		const auto diff = glm::distance(v.pos, center) - radius;
		if (diff > 0) {
			radius += diff;
		}
	}

	return Sphere{ center, radius };
}

void transformVertices(Model& model,
	const std::array<uint8_t, FrameData().payload.size()>& clientData,
	uint8_t* buffer,
	std::size_t bufsize,
	int& nVertices,
	int& nIndices)
{
	Camera camera;
	{
		// TODO: make this a function (but have to come up with a consistent framework
		// of client serialize/deserialize funcs first)
		const auto shCam = *reinterpret_cast<const shared::Camera*>(clientData.data());
		camera.position.x = shCam.x;
		camera.position.y = shCam.y;
		camera.position.z = shCam.z;
		camera.yaw = shCam.yaw;
		camera.pitch = shCam.pitch;
		camera.updateVectors();
	}

	// STUB
	wiggle(model);

	const auto& frustum = calcFrustum(/*camera.projMatrix()*/ glm::mat4{ 1.f });   // TODO
	const auto sphere = calcBoundingSphere(model);

	// std::cerr << "bounding sphere = " << sphere.center << ", r = " << sphere.radius << "\n";
	// std::cerr << "frustum = " << frustum.left << ", " << frustum.right << ", " <<
	// frustum.bottom << ", " << frustum.top << ", " << frustum.near << ", " << frustum.far << "\n";

	std::unordered_map<Index, Index> indexRemap;
	{
		// Filter model vertices and copy the good ones to the temp memory area (i.e. `buffer`)
		int vertexIdx = 0;
		auto verticesBuffer = reinterpret_cast<Vertex*>(buffer);
		for (unsigned i = 0; i < model.nVertices; ++i) {
			const auto& v = model.vertices[i];
			const auto vv = camera.viewMatrix() * glm::vec4{ v.pos.x, v.pos.y, v.pos.z, 1.0 };
			if (/* FIXME */ true /*|| sphereInFrustum(glm::vec3{ vv }, sphere.radius * 2, frustum)*/) {
				assert(sizeof(Vertex) * vertexIdx < bufsize &&
					"transformVertices: writing in unowned memory area!");
				verticesBuffer[vertexIdx] = v;
				indexRemap[i] = vertexIdx;
				// if (!sphereInFrustum(vv, sphere.radius, frustum))
				// verticesBuffer[vertexIdx].color = glm::vec3{ 1.f, 0.f, 0.f };
				++vertexIdx;
			}
		}
		nVertices = vertexIdx;
	}

	{
		// Remap all indices
		int indexIdx = 0;
		auto indicesBuffer = reinterpret_cast<Index*>(buffer + nVertices * sizeof(Vertex));
		for (unsigned i = 0; i < model.nIndices; ++i) {
			const auto index = model.indices[i];
			const auto it = indexRemap.find(index);
			if (it == indexRemap.end()) {
				// Drop the index, it's not used anymore
				continue;
			}
			assert(nVertices * sizeof(Vertex) + sizeof(Index) * indexIdx < bufsize &&
				"transformVertices: writing in unowned memory area!");
			indicesBuffer[indexIdx] = it->second;
			++indexIdx;
		}
		nIndices = indexIdx;
	}
}

void appstageLoop(Server& server)
{
	using namespace std::literals::chrono_literals;

	std::default_random_engine rng;
	rng.seed(std::random_device{}());

	auto& model = server.resources.models.begin()->second;
	auto updates = buildUpdatePackets(model);
	std::vector<std::size_t> idxToPick(updates.size());
	for (unsigned i = 0; i < idxToPick.size(); ++i)
		idxToPick[i] = i;

	std::uniform_int_distribution<unsigned> nSentDist{ 1, 100 };
	float t = 0;

	Clock clock;
	auto beginTime = std::chrono::high_resolution_clock::now();

	while (true) {
		const LimitFrameTime lft{ 33ms };

		// Model updating simulation
		/*
		// Wiggle the model and schedule random portions of it to be updated
		// wiggle(model);

		// FIXME: simple but inefficient way to send random updates: we're creating all
		// the update chunks and discarding most of them at each frame.
		updates = buildUpdatePackets(model);

		std::shuffle(idxToPick.begin(), idxToPick.end(), rng);

		const auto nSent = nSentDist(rng);
		{
			std::lock_guard<std::mutex> lock{ server.shared.geomUpdateMtx };
			for (unsigned i = 0; i < nSent; ++i) {
				assert(i < idxToPick.size() && idxToPick[i] < updates.size());
				server.shared.geomUpdate.emplace_back(updates[idxToPick[i]]);
			}
		}
		server.shared.geomUpdateCv.notify_one();
		*/

		// Move dyn lights
		auto& light = server.resources.pointLights[0];
		t += clock.deltaTime();
		light.position = glm::vec3{ 10 * std::sin(t), 5 + 5 * std::sin(t * 0.7), 10 * std::cos(t) };
		light.color = glm::vec3{
			0.5 + 0.5 * std::sin(t), 0.5 + 0.5 * std::sin(t * 0.33), 0.5 + 0.5 * std::cos(t * 0.66)
		};
		light.intensity = std::abs(4 * std::sin(t * 0.75));

		// debug("t = ", t, ", light: ", toString(light));

		const auto endTime = std::chrono::high_resolution_clock::now();
		float dt = std::chrono::duration_cast<std::chrono::microseconds>(endTime - beginTime).count() /
			   1'000'000.f;
		if (dt > 1.f)
			dt = clock.targetDeltaTime;
		clock.update(dt);
		beginTime = endTime;
	}
}
