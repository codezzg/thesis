#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 inPos;

layout (location = 0) out vec3 outTexCoords;

layout (set = 0, binding = 0) uniform ViewUniformBufferObject {
	mat4 view;
	mat4 proj;
} viewUbo;

void main() {
	outTexCoords = inPos;
	gl_Position = viewUbo.proj * viewUbo.view * vec4(inPos, 1.0);
}
