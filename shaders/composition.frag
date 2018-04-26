#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 texCoords;

layout (location = 0) out vec4 fragColor;

layout (set = 0, binding = 0) uniform sampler2D gPosition;
layout (set = 0, binding = 1) uniform sampler2D gNormal;
layout (set = 0, binding = 2) uniform sampler2D gAlbedoSpec;
layout (set = 0, binding = 3) uniform CompositionUniformBuffer {
	// TODO research https://www.khronos.org/opengl/wiki/Interface_Block_(GLSL)#Memory_layout
	// about avoiding using vec3
	vec4 viewPos; // w is unused
} ubo;

void main() {
	vec3 fragPos = texture(gPosition, texCoords).rgb;
	vec3 normal = texture(gNormal, texCoords).rgb;
	vec3 albedo = texture(gAlbedoSpec, texCoords).rgb;
	float specular = texture(gAlbedoSpec, texCoords).a;

	const float ambient = 0.1;
	vec3 lighting = albedo * ambient;
	vec3 viewDir = normalize(ubo.viewPos.xyz - fragPos);

	// For now just 1 light, hardcoded
	const vec3 lightPos = vec3(10.0, 50.0, 1.0);
	const vec3 lightColor = vec3(1.0, 1.0, 1.0);

	vec3 lightDir = normalize(lightPos - fragPos);
	vec3 diffuse = max(dot(normal, lightDir), 0.0) * albedo * lightColor;
	lighting += diffuse;

	/*fragColor = vec4(lighting, 1.0);*/
	fragColor = vec4(albedo, 1.0);
	/*fragColor = vec4(texCoords, 0.0, 1.0);*/
}
