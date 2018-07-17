#pragma once

#include <glm/glm.hpp>

/** This UBO contains the per-model data */
struct ObjectUBO {
	glm::mat4 model;
};

/** Representation of a PointLight inside a uniform buffer */
struct UboPointLight {
	glm::vec3 position;
	float attenuation;
	glm::vec3 color;
	float _padding;
};

/** This UBO contains the per-view data */
struct ViewUBO {

	// Camera stuff
	glm::mat4 viewProj;
	glm::vec3 viewPos;
	float _padding;

	// Shader options
	glm::i32 opts;   // showGBufTex | useNormalMap
};

struct LightsUBO {
	static constexpr auto MAX_LIGHTS = 200;

	UboPointLight pointLights[MAX_LIGHTS];
	glm::i32 nPointLights;
};
