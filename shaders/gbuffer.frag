#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNorm;
layout (location = 2) in vec2 inTexCoords;
layout (location = 3) in vec3 inTangent;
layout (location = 4) in vec3 inBitangent;

// G-buffer content
layout (location = 0) out vec3 outPos;
layout (location = 1) out vec3 outNorm;
layout (location = 2) out vec4 outAlbedoSpec;

layout (set = 2, binding = 0) uniform sampler2D texDiffuse;
layout (set = 2, binding = 1) uniform sampler2D texSpecular;
layout (set = 2, binding = 2) uniform sampler2D texNormal;

struct PointLight {
	vec3 position;
	float intensity;
	vec4 color;
};

layout (set = 0, binding = 0) uniform ViewUniformBuffer {
	// unused
	PointLight pointLight;

	// unused
	mat4 view;
	// unused
	mat4 proj;

	// TODO research https://www.khronos.org/opengl/wiki/Interface_Block_(GLSL)#Memory_layout
	// about avoiding using vec3
	vec4 viewPos;
	int opts;
} ubo;

void main() {
	outPos = inPos;
	outAlbedoSpec.rgb = texture(texDiffuse, inTexCoords).rgb;
	outAlbedoSpec.a = texture(texSpecular, inTexCoords).r;

	vec3 t = normalize(inTangent);
	vec3 n = normalize(inNorm);
	vec3 b = normalize(inBitangent);
	mat3 tbn = mat3(t, b, n);
	if (((ubo.opts >> 1) & 1) != 0) // use normal map
		outNorm = tbn * normalize(texture(texNormal, inTexCoords).xyz * 2.0 - vec3(1.0));
	else
		outNorm = inNorm;

	/*outAlbedoSpec.rg = inTexCoords;*/
	/*outAlbedoSpec.b = 0.0;*/
}
