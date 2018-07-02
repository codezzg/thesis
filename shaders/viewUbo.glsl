#pragma include PointLight.glsl

layout (set = 0, binding = 0) uniform ViewUniformBuffer {
	PointLight pointLight;

	mat4 viewProj;

	// TODO research https://www.khronos.org/opengl/wiki/Interface_Block_(GLSL)#Memory_layout
	// about avoiding using vec3
	vec4 viewPos;
	// bitset
	// 0: showGbufTex
	// 1: useNormalMap
	int opts;
} viewUbo;
