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

#pragma include viewUbo.glsl

void main() {
	outPos = inPos;
	outAlbedoSpec.rgb = texture(texDiffuse, inTexCoords).rgb;
	outAlbedoSpec.a = texture(texSpecular, inTexCoords).r;

	vec3 t = normalize(inTangent);
	vec3 n = normalize(inNorm);
	vec3 b = normalize(inBitangent);
	mat3 tbn = mat3(t, b, n);
	if (((viewUbo.opts >> 1) & 1) != 0) // use normal map
		outNorm = tbn * normalize(texture(texNormal, inTexCoords).xyz * 2.0 - vec3(1.0));
	else
		outNorm = inNorm;

	/*outAlbedoSpec.rg = inTexCoords;*/
	/*outAlbedoSpec.b = 0.0;*/
}
