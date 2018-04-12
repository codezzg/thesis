#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNorm;
layout (location = 2) in vec2 inTexCoords;

// G-buffer content
layout (location = 0) out vec3 outPos;
layout (location = 1) out vec3 outNorm;
layout (location = 2) out vec4 outAlbedoSpec;

layout (binding = 0) uniform sampler2D texDiffuse;
layout (binding = 1) uniform sampler2D texSpecular;

void main() {
	outPos = inPos;
	outNorm = normalize(inNorm);
	outAlbedoSpec.rgb = texture(texDiffuse, inTexCoords).rgb;
	outAlbedoSpec.a = texture(texSpecular, inTexCoords).r;
}
