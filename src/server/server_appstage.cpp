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

static std::vector<QueuedUpdate> enqueueModelsGeomUpdates(const std::vector<Model>& modelsToSend)
{
	std::vector<QueuedUpdate> updates;
	for (const auto& model : modelsToSend) {
		const auto updatePackets = buildUpdatePackets(model);
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
					node->transform.rotation = glm::vec3{ 0.f, 0.3f * t + i, 0.f };
					node->transform.scale =
						glm::vec3{ 1 + std::max(-0.2, std::abs(std::cos(t * 0.5))),
							1 + std::max(-0.2, std::abs(std::cos(t * 0.5))),
							1 + std::max(-0.2, std::abs(std::cos(t * 0.5))) };
					node->transform._update();
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
