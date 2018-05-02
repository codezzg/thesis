#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inTexCoord;

out gl_PerVertex {
	vec4 gl_Position;
};

// World-space coordinates
layout (location = 0) out vec3 outPos;
layout (location = 1) out vec3 outNorm;
layout (location = 2) out vec2 outTexCoords;


vec3 positions[3] = vec3[](
	vec3(0.0, -0.5, 0.0),
	vec3(0.5, 0.5, 0.0),
	vec3(-0.5, 0.5, 0.0)
);

vec3 normals[3] = vec3[](
	vec3(1.0, 0.0, 0.0),
	vec3(0.0, 1.0, 0.0),
	vec3(0.0, 0.0, 1.0)
);

vec2 texCoord[3] = vec2[](
	vec2(1.0, 0.0),
	vec2(0.0, 1.0),
	vec2(0.0, 0.0)
);


void main() {
	gl_Position = vec4(positions[gl_VertexIndex], 1.0);
	outPos = positions[gl_VertexIndex];
	outNorm = normals[gl_VertexIndex];
	outTexCoords = texCoord[gl_VertexIndex];
}
