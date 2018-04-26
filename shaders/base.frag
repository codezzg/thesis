#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 inTexCoords;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D tex;

void main() {
	vec3 col = texture(tex, inTexCoords).rgb;
	outColor = vec4(col, 1.0);
}
