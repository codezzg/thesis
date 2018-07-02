#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 inPos;

layout (location = 0) out vec3 outTexCoords;

/*layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput gPosition;*/
/*layout (input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput gNormal;*/
/*layout (input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput gAlbedoSpec;*/

#pragma include viewUbo.glsl

void main() {
	outTexCoords = inPos;
	gl_Position = viewUbo.viewProj * vec4(inPos, 1.0);
}
