#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNorm;
layout (location = 2) in vec2 inTexCoords;

out gl_PerVertex {
	vec4 gl_Position;
};

// World-space coordinates
layout (location = 0) out vec3 outPos;
layout (location = 1) out vec3 outNorm;
layout (location = 2) out vec2 outTexCoords;

layout (set = 3, binding = 0) uniform UniformBufferObject {
	mat4 model;
	mat4 view;
	mat4 proj;
} ubo;

void main() {
	vec4 worldPos = ubo.model * vec4(inPos, 1.0);
	outPos = worldPos.xyz;
	outTexCoords = inTexCoords;

	mat3 normalMat = transpose(inverse(mat3(ubo.model)));
	outNorm = normalMat * inNorm;

	gl_Position = ubo.proj * ubo.view * worldPos;
}
