#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 texCoords;

layout (location = 0) out vec4 fragColor;

layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput gPosition;
layout (input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput gNormal;
layout (input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput gAlbedoSpec;

struct PointLight {
	vec3 position;
	float intensity;
	vec4 color;
};

layout (set = 0, binding = 0) uniform CompositionUniformBuffer {
	PointLight pointLight;

	// TODO research https://www.khronos.org/opengl/wiki/Interface_Block_(GLSL)#Memory_layout
	// about avoiding using vec3
	vec4 viewPos;
	// bitset
	// 0: showGbufTex
	// 1: useNormalMap
	int opts;
} ubo;

#define AMBIENT_INTENSITY 0.45

void main() {
	// For now, hardcode ambient color
	const vec3 ambientColor = vec3(1.0);
	PointLight pointLight = ubo.pointLight;// PointLight(vec3(10.0, 10.0, 10.0), vec3(1.0, 1.0, 1.0), 1.0);

	// TODO: use material shininess
	const float shininess = 32.0;

	// Load GBuffer content
	vec3 fragPos = subpassLoad(gPosition).rgb;
	vec3 normal = subpassLoad(gNormal).rgb;
	vec3 albedo = subpassLoad(gAlbedoSpec).rgb;
	float fragSpec = subpassLoad(gAlbedoSpec).a;

	vec3 lightFragVec = fragPos - pointLight.position.xyz;
	float lightFragDist = length(lightFragVec);
	// FIXME wats dis
	float attenuation = 100.0 * pointLight.intensity / pow(lightFragDist, 2.0);

	// Ambient
	float isSky = float(length(fragPos) > 1000.0); // HACK!
	vec3 ambient = (isSky + (1.0 - isSky) * AMBIENT_INTENSITY) * ambientColor * albedo;

	// Diffuse
	vec3 lightDir = normalize(lightFragVec);
	float diff = max(dot(normal, lightDir), 0.0);
	vec3 diffuse = pointLight.color.rgb * diff * albedo;
	diffuse *= attenuation;

	// Specular
	vec3 viewDir = normalize(ubo.viewPos.xyz - fragPos);
	vec3 reflectDir = reflect(-lightDir, normal);

	vec3 halfDir = normalize(lightDir + viewDir);
	float spec = pow(max(dot(normal, halfDir), 0.0), shininess);
	vec3 specular = pointLight.color.rgb * spec * fragSpec;
	specular *= attenuation;

	vec3 lighting = ambient + diffuse + specular;

	bool showGBufTexs = (ubo.opts & 1) != 0;
	if (showGBufTexs) {
		if (texCoords.x < 0.5) {
			if (texCoords.y < 0.5)
				fragColor = vec4(lighting, 1.0);
			else
				fragColor = vec4(albedo, 1.0);
		} else {
			if (texCoords.y < 0.5)
				fragColor = vec4((1.0 + normal) * 0.5, 1.0);
			else
				fragColor = vec4(vec3(fragSpec), 1.0);
		}
	} else {
		/*if (length(fragPos) < 10000.0) {*/
			/*fragColor = vec4(abs(fragPos.x) / 10.0, abs(fragPos.y) / 50.0, abs(fragPos.z) / 10.0, 1.0);*/
			/*fragColor = vec4(vec3(100.0 / pow(lightFragDist, 2.0)), 1.0);*/
			/*fragColor = vec4(vec3(pointLight.intensity), 1.0);*/
		/*} else*/
			/*fragColor = vec4(0.0);*/
		fragColor = vec4(lighting, 1.0);
	}
	/*fragColor = vec4(lighting, 1.0);*/
	/*fragColor = vec4(vec3(specular), 1.0);*/
	/*fragColor = vec4(normal, 1.0);*/
	/*fragColor = vec4(texCoords, 0.0, 1.0);*/
	/*fragColor = vec4(fragPos, 1.0);*/
}
