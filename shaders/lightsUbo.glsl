#pragma include PointLight.glsl

#define MAX_LIGHTS 200

layout (set = 0, binding = 2) uniform LightsUbo {
	PointLight pointLights[MAX_LIGHTS];
	int nPointLights;
} lightsUbo;
