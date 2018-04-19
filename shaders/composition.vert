#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 inPos;
layout (location = 2) in vec2 inTexCoords;

out gl_PerVertex {
	vec4 gl_Position;
};

layout (location = 0) out vec2 texCoords;

void main() {
	texCoords = inTexCoords;
	gl_Position = vec4(inPos, 1.0);
}
