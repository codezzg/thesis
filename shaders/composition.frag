#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 texCoords;

layout (location = 0) out vec4 fragColor;

layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput gPosition;
layout (input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput gNormal;
layout (input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput gAlbedoSpec;
layout (set = 0, binding = 0) uniform CompositionUniformBuffer {
	// TODO research https://www.khronos.org/opengl/wiki/Interface_Block_(GLSL)#Memory_layout
	// about avoiding using vec3
	vec4 viewPos; // w is used as 'showGBufTexture'
} ubo;

#define AMBIENT_INTENSITY 0.05

void main() {
	// For now just 1 light, hardcoded
	const vec3 lightPos = vec3(10.0, 50.0, 1.0);
	const vec3 lightColor = vec3(1.0, 1.0, 1.0);

	// For now, hardcode ambient color
	const vec3 ambientColor = vec3(1.0);

	// TODO: use material shininess
	const float shininess = 32.0;

	// Load GBuffer content
	vec3 fragPos = subpassLoad(gPosition).rgb;
	vec3 normal = subpassLoad(gNormal).rgb;
	vec3 albedo = subpassLoad(gAlbedoSpec).rgb;
	float fragSpec = subpassLoad(gAlbedoSpec).a;

	// Ambient
	const vec3 ambient = AMBIENT_INTENSITY * ambientColor * albedo;

	// Diffuse
	vec3 lightDir = normalize(lightPos - fragPos);
	float diff = max(dot(normal, lightDir), 0.0);
	vec3 diffuse = lightColor * diff * albedo;

	// Specular
	vec3 viewDir = normalize(ubo.viewPos.xyz - fragPos);
	vec3 reflectDir = reflect(-lightDir, normal);

	vec3 halfDir = normalize(lightDir + viewDir);
	float spec = pow(max(dot(normal, halfDir), 0.0), shininess);
	vec3 specular = lightColor * spec * fragSpec;

	vec3 lighting = ambient + diffuse + specular;

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
				fragColor = vec4(vec3(fragSpec), 1.0);
		}
	} else {
		fragColor = vec4(lighting, 1.0);
		/*fragColor = vec4(albedo, 1.0);*/
	}
	/*fragColor = vec4(lighting, 1.0);*/
	/*fragColor = vec4(vec3(specular), 1.0);*/
	/*fragColor = vec4(normal, 1.0);*/
	/*fragColor = vec4(texCoords, 0.0, 1.0);*/
	/*fragColor = vec4(fragPos, 1.0);*/
}
