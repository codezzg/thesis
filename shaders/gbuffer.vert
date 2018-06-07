#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNorm;
layout (location = 2) in vec2 inTexCoords;
layout (location = 3) in vec3 inTangent;
layout (location = 4) in vec3 inBitangent;

out gl_PerVertex {
	vec4 gl_Position;
};

// World-space coordinates
layout (location = 0) out vec3 outPos;
layout (location = 1) out vec3 outNorm;
layout (location = 2) out vec2 outTexCoords;
layout (location = 3) out vec3 outTangent;
layout (location = 4) out vec3 outBitangent;

layout (set = 3, binding = 0) uniform UniformBufferObject {
	mat4 model;
	mat4 view;
	mat4 proj;
} mvpUbo;

void main() {
	// TODO
	const vec3 lightPos = vec3(10.0, 50.0, 1.0);

	vec4 worldPos = mvpUbo.model * vec4(inPos, 1.0);
	outPos = worldPos.xyz;
	outTexCoords = inTexCoords;

	mat3 normalMat = transpose(inverse(mat3(mvpUbo.model)));
	outNorm = normalMat * inNorm;
	outTangent = normalMat * inTangent;
	outBitangent = normalMat * inBitangent;

	gl_Position = mvpUbo.proj * mvpUbo.view * worldPos;
}
