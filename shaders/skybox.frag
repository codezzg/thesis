#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 inTexCoords;

layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 1) uniform samplerCube skybox;

void main() {
	outFragColor = texture(skybox, inTexCoords);
}
