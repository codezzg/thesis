#version 450
#extension GL_ARB_separate_shader_objects : enable

/*precision mediump int; precision highp float;*/

layout (location = 0) in vec2 texCoords;

layout (location = 0) out vec4 fragColor;

layout (input_attachment_index = 0, set = 0, binding = 3) uniform subpassInput gPosition;
layout (input_attachment_index = 1, set = 0, binding = 4) uniform subpassInput gNormal;
layout (input_attachment_index = 2, set = 0, binding = 5) uniform subpassInput gAlbedoSpec;
layout (set = 0, binding = 6) uniform CompositionUniformBuffer {
	// TODO research https://www.khronos.org/opengl/wiki/Interface_Block_(GLSL)#Memory_layout
	// about avoiding using vec3
	vec4 viewPos; // w is used as 'showGBufTexture'
} ubo;

#define AMBIENT_LIGHT 0.3

void main() {
	vec3 fragPos = subpassLoad(gPosition).rgb;
	vec3 normal = subpassLoad(gNormal).rgb;
	vec3 albedo = subpassLoad(gAlbedoSpec).rgb;
	float specular = subpassLoad(gAlbedoSpec).a;

	const float ambient = AMBIENT_LIGHT;
	vec3 lighting = albedo * ambient;
	vec3 viewDir = normalize(ubo.viewPos.xyz - fragPos);

	// For now just 1 light, hardcoded
	const vec3 lightPos = vec3(10.0, 50.0, 1.0);
	const vec3 lightColor = vec3(1.0, 0.0, 1.0);

	vec3 lightDir = normalize(lightPos - fragPos);
	vec3 diffuse = max(dot(normal, lightDir), 0.0) * albedo * lightColor;
	lighting += diffuse;

	bool showGBufTexs = ubo.viewPos.w != 0.0;
	if (showGBufTexs) {
		if (texCoords.x < 0.5) {
			if (texCoords.y < 0.5)
				fragColor = vec4(lighting, 1.0);
			else
				fragColor = vec4(albedo, 1.0);
		} else {
			if (texCoords.y < 0.5)
				fragColor = vec4(normal, 1.0);
			else
				fragColor = vec4(vec3(specular), 1.0);
		}
	} else {
		fragColor = vec4(lighting, 1.0);
	}
	/*fragColor = vec4(lighting, 1.0);*/
	/*fragColor = vec4(vec3(specular), 1.0);*/
	/*fragColor = vec4(albedo, 1.0);*/
	/*fragColor = vec4(normal, 1.0);*/
	/*fragColor = vec4(texCoords, 0.0, 1.0);*/
	/*fragColor = vec4(fragPos, 1.0);*/
}
