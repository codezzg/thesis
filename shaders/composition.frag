#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 texCoords;

layout (location = 0) out vec4 fragColor;

layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput gPosition;
layout (input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput gNormal;
layout (input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput gAlbedoSpec;

#pragma include viewUbo.glsl
#pragma include lightsUbo.glsl

#define AMBIENT_INTENSITY 0.45

vec3 addPointLight(in PointLight light,
		in vec3 fragPos,
		in vec3 viewPos,
		in vec3 objDiffuse,
		in vec3 objSpecular,
		in float objSpecPower,
		in vec3 objNormal)
{
	// diffuse
	vec3 norm = normalize(objNormal);
	vec3 fragToLight = light.position - fragPos;
	vec3 lightDir = normalize(fragToLight);
	float diff = max(dot(norm, lightDir), 0.0);
	vec3 diffuse = diff * light.color * objDiffuse;

	// specular
	vec3 viewDir = normalize(viewPos - fragPos);
	vec3 halfDir = normalize(lightDir + viewDir);
	float spec = pow(max(dot(halfDir, norm), 0.0), max(1.0, 32 * objSpecPower));
	vec3 specular = objSpecular * spec * light.color;

	vec3 result = diffuse + specular;

	// attenuation
	float dist = length(fragToLight);
	float atten = 1.0 / (1.0 + light.attenuation * dist * dist);

	return result * atten;
}

void main() {
	// For now, hardcode ambient color
	const vec3 ambientColor = vec3(1.0);

	// Load GBuffer content
	vec3 fragPos = subpassLoad(gPosition).rgb;
	vec3 normal = subpassLoad(gNormal).rgb;
	vec3 albedo = subpassLoad(gAlbedoSpec).rgb;
	float spec = subpassLoad(gAlbedoSpec).a;

	// Ambient
	/*float isSky = float(fragPos.z > 1000.0); // HACK!*/
	/*vec3 ambient = (isSky + (1.0 - isSky) * AMBIENT_INTENSITY) * ambientColor * albedo;*/
	vec3 ambient = ambientColor * albedo * AMBIENT_INTENSITY;

	vec3 lighting = ambient;

	for (int i = 0; i < lightsUbo.nPointLights; ++i) {
		lighting += addPointLight(lightsUbo.pointLights[i], fragPos, viewUbo.viewPos,
				albedo, vec3(1.0, 1.0, 1.0), spec, normal);
	}

	bool showGBufTexs = (viewUbo.opts & 1) != 0;
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
				fragColor = vec4(vec3(spec), 1.0);
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
