#include "server_appstage.hpp"
#include "clock.hpp"
#include "fps_counter.hpp"
#include "frame_utils.hpp"
#include "geom_update.hpp"
#include "logging.hpp"
#include "server.hpp"
#include "to_string.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>
#include <iostream>
#include <random>
#include <unordered_map>
#include <vector>

using namespace logging;

extern bool gMoveObjects;
extern bool gChangeLights;

struct Sphere {
	glm::vec3 center;
	float radius;
};

static std::vector<QueuedUpdate> enqueueModelsGeomUpdates(const std::vector<const Model*>& modelsToSend)
{
	std::vector<QueuedUpdate> updates;
	for (const auto& model : modelsToSend) {
		const auto updatePackets = buildUpdatePackets(*model);
		for (const auto& up : updatePackets) {
			updates.emplace_back(newQueuedUpdateGeom(up));
		}
	}
	return updates;
}

void appstageLoop(Server& server)
{
	using namespace std::literals::chrono_literals;

	float t = 0;

	Clock clock;
	auto beginTime = std::chrono::high_resolution_clock::now();
	FPSCounter fps{ "Appstage" };
	fps.reportPeriod = 5;

	while (true) {
		const LimitFrameTime lft{ 33ms };

		// Persistent updates to add this frame
		std::vector<QueuedUpdate> pUpdates;
		// Transitory updates to add this frame
		std::vector<QueuedUpdate> tUpdates;

		if (server.toClient.modelsToSend.size() > 0) {
			std::lock_guard<std::mutex> lock{ server.toClient.modelsToSendMtx };
			const auto geomUpdates = enqueueModelsGeomUpdates(server.toClient.modelsToSend);
			pUpdates.insert(pUpdates.end(), geomUpdates.begin(), geomUpdates.end());
			server.toClient.modelsToSend.clear();
		}

		// Change point lights
		int i = 0;
		bool notify = false;

		if (gChangeLights) {
			i = 0;
			for (auto& light : server.resources.pointLights) {
				light.color = glm::vec3{ 0.5 + 0.5 * std::sin(t + i * 0.3),
					0.5 + 0.5 * std::sin(t * 0.33 + i * 0.4),
					0.5 + 0.5 * std::cos(t * 0.66 + i * 0.56) };
				light.attenuation = 0.1 + std::abs(0.3 * std::sin(t * 0.75 + i * 0.23));
				tUpdates.emplace_back(newQueuedUpdatePointLight(light.name));
				++i;
			}
			notify = true;
		}

		// Move objects
		if (gMoveObjects) {
			i = 0;
			for (auto node : server.scene.nodes) {
				if (node->type == NodeType::EMPTY)
					continue;

				// node->transform.position = glm::vec3{ 0, 0, i * 5 };
				if (((node->flags >> NODE_FLAG_STATIC) & 1) == 0) {
					node->transform.position =
						glm::vec3{ (5 + 0 * 4 * i) * std::sin(0.5 * t + i * 0.4),
							(5 + 0 * 2 * i) * std::sin(0.5 * t + i * 0.4),
							// 10 - 9 * (node->type == NodeType::POINT_LIGHT),
							(2 + 0 * 6 * i) * std::cos(0.5 * t + i * 0.3) };
					node->transform.rotation = glm::vec3{ 0, 0.3 * t + i, 0 };
					node->transform.scale =
						glm::vec3{ 1 + std::max(-0.2, i * std::abs(std::cos(t * 0.5))),
							1 + std::max(-0.2, i * std::abs(std::cos(t * 0.5))),
							1 + std::max(-0.2, i * std::abs(std::cos(t * 0.5))) };
				}
				tUpdates.emplace_back(newQueuedUpdateTransform(node->name));
				++i;
			}
			notify = true;
		}

		{
			std::lock_guard<std::mutex> lock{ server.toClient.updates.mtx };
			server.toClient.updates.transitory.assign(tUpdates.begin(), tUpdates.end());
			if (pUpdates.size() > 0)
				verbose("adding ", pUpdates.size(), " pUpdates");

			for (const auto& u : pUpdates) {
				switch (u.type) {
				case QueuedUpdate::Type::GEOM:
					if (server.toClient.updates.persistent.load_factor() > 0.95) {
						err("Map's load factor is too high! Please give more memory ",
							"to persistent updates's hashmap!");
						return;
					}
					server.toClient.updates.persistent.set(
						u.data.geom.data.serialId, u.data.geom.data.serialId, u);
					break;
				default:
					err("Invalid persistent update type: ", int(u.type));
					break;
				}
			}
		}

		if (notify)
			server.toClient.updates.cv.notify_one();

		// Update clock
		t += clock.deltaTime();
		const auto endTime = std::chrono::high_resolution_clock::now();
		float dt = std::chrono::duration_cast<std::chrono::microseconds>(endTime - beginTime).count() /
			   1'000'000.f;
		if (dt > 1.f)
			dt = clock.targetDeltaTime;
		clock.update(dt);
		beginTime = endTime;

		fps.addFrame();
		fps.report();
	}
	info("Server appstage loop exited.");
}

#if 0
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
#endif
