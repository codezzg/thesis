#include "tcp_deserialize.hpp"
#include "client_resources.hpp"
#include "config.hpp"
#include "logging.hpp"
#include "shared_resources.hpp"
#include "utils.hpp"
#include <algorithm>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

using namespace logging;

bool receiveTexture(socket_t socket,
	const uint8_t* buffer,
	std::size_t bufsize,
	/* out */ ClientTmpResources& resources)
{
	// Parse header

	const auto header = *reinterpret_cast<const ResourcePacket<shared::TextureInfo>*>(buffer);
	constexpr auto sizeOfHeader = sizeof(ResourcePacket<shared::TextureInfo>);
	const auto expectedSize = header.res.size;

	if (expectedSize > cfg::MAX_TEXTURE_SIZE) {
		err("Texture server sent is too big! (", expectedSize / 1024 / 1024., " MiB)");
		return false;
	}

	const auto texName = header.res.name;

	assert(static_cast<uint8_t>(header.res.format) < static_cast<uint8_t>(shared::TextureFormat::UNKNOWN));

	// Retreive payload

	/** Obtain the memory to store the texture data in */
	void* texdata = resources.allocator.alloc(expectedSize);
	if (!texdata)
		return false;

	// Copy the first texture data embedded in the header packet into the texture memory area
	auto len = std::min(bufsize - sizeOfHeader, expectedSize);
	memcpy(texdata, buffer + sizeOfHeader, len);

	// Receive remaining texture data as raw data packets (if needed)
	auto processedSize = len;
	while (processedSize < expectedSize) {
		const auto remainingSize = expectedSize - processedSize;
		assert(remainingSize > 0);

		len = std::min(remainingSize, bufsize);

		int bytesRead;
		// Receive the data directly into the texture memory area (avoids a memcpy from the buffer)
		if (!receivePacket(socket, reinterpret_cast<uint8_t*>(texdata) + processedSize, len, &bytesRead)) {
			resources.allocator.deallocLatest();
			return false;
		}

		processedSize += bytesRead;
	}

	if (processedSize != expectedSize) {
		warn("Processed more bytes than expected!");
	}

	shared::Texture texture;
	texture.size = expectedSize;
	texture.data = texdata;
	texture.format = header.res.format;

	if (resources.textures.count(texName) > 0) {
		warn("Received the same texture two times: ", texName);
	} else {
		resources.textures[texName] = texture;
		info("Stored texture ", texName);
	}

	info("Received texture ", texName, ": ", texture.size, " B");
	if (gDebugLv >= LOGLV_VERBOSE) {
		dumpBytes(texture.data, texture.size);
	}

	return true;
}

bool receiveMaterial(const uint8_t* buffer,
	std::size_t bufsize,
	/* out */ ClientTmpResources& resources)
{
	assert(bufsize >= sizeof(ResourcePacket<shared::Material>));
	static_assert(sizeof(StringId) == 4, "StringId size should be 4!");

	// Parse header
	const auto material = *reinterpret_cast<const shared::Material*>(buffer + 1);

	debug("received material: { name = ",
		material.name,
		", diff = ",
		material.diffuseTex,
		", spec = ",
		material.specularTex,
		", norm = ",
		material.normalTex,
		" }");

	if (std::find_if(
		    resources.materials.begin(), resources.materials.end(), [name = material.name](const auto& mat) {
			    return mat.name == name;
		    }) != resources.materials.end()) {
		warn("Received the same material two times: ", material.name);
	} else {
		resources.materials.emplace_back(material);
		info("Stored material ", material.name);
	}

	return true;
}

bool receiveModel(socket_t socket,
	const uint8_t* buffer,
	std::size_t bufsize,
	/* out */ ClientTmpResources& resources)
{
	assert(bufsize >= sizeof(ResourcePacket<shared::Model>));

	// Parse header
	const auto header = *reinterpret_cast<const ResourcePacket<shared::Model>*>(buffer);
	constexpr auto sizeOfHeader = sizeof(ResourcePacket<shared::Model>);
	const auto expectedSize = header.res.nMaterials * sizeof(StringId) + header.res.nMeshes * sizeof(shared::Mesh);

	if (expectedSize > cfg::MAX_MODEL_INFO_SIZE) {
		err("Model server sent is too big! (", expectedSize / 1024 / 1024., " MiB)");
		return false;
	}

	// Retreive payload [materials | meshes]

	void* payload = resources.allocator.alloc(expectedSize);
	if (!payload)
		return false;

	// Copy the first texture data embedded in the header packet into the texture memory area
	auto len = std::min(bufsize - sizeOfHeader, expectedSize);
	memcpy(payload, buffer + sizeOfHeader, len);

	// Receive remaining model information as raw data packets (if needed)
	auto processedSize = len;
	while (processedSize < expectedSize) {
		const auto remainingSize = expectedSize - processedSize;
		assert(remainingSize > 0);

		len = std::min(remainingSize, bufsize);

		if (!receivePacket(socket, reinterpret_cast<uint8_t*>(payload) + processedSize, len)) {
			resources.allocator.deallocLatest();
			return false;
		}

		processedSize += len;
	}

	if (processedSize != expectedSize) {
		warn("Processed more bytes than expected!");
	}

	ModelInfo model;
	model.name = header.res.name;
	model.nVertices = header.res.nVertices;
	model.nIndices = header.res.nIndices;

	model.materials.reserve(header.res.nMaterials);
	const auto materials = reinterpret_cast<const StringId*>(payload);
	for (unsigned i = 0; i < header.res.nMaterials; ++i)
		model.materials.emplace_back(materials[i]);

	model.meshes.reserve(header.res.nMeshes);
	const auto meshes = reinterpret_cast<const shared::Mesh*>(
		reinterpret_cast<uint8_t*>(payload) + header.res.nMaterials * sizeof(StringId));
	for (unsigned i = 0; i < header.res.nMeshes; ++i)
		model.meshes.emplace_back(meshes[i]);

	if (std::find_if(resources.models.begin(), resources.models.end(), [name = model.name](const auto& m) {
		    return m.name == name;
	    }) != resources.models.end()) {
		warn("Received the same model two times: ", model.name);
	} else {
		resources.models.emplace_back(model);
		info("Stored model ", model.name);
	}

	debug("received model ", model.name, " (v=", model.nVertices, ", i=", model.nIndices, "):");
	if (gDebugLv >= LOGLV_DEBUG) {
		for (const auto& mat : model.materials)
			debug("material ", mat);

		for (const auto& mesh : model.meshes) {
			debug("mesh { off = ",
				mesh.offset,
				", len = ",
				mesh.len,
				", mat = ",
				mesh.materialId,
				" (",
				mesh.materialId >= 0 ? model.materials[mesh.materialId] : SID_NONE,
				") }");
		}
	}

	return true;
}

bool receivePointLight(const uint8_t* buffer,
	std::size_t bufsize,
	/* out */ ClientTmpResources& resources)
{
	assert(bufsize >= sizeof(ResourcePacket<shared::PointLightInfo>));

	// Parse header
	const auto lightInfo = *reinterpret_cast<const shared::PointLightInfo*>(buffer + 1);

	shared::PointLight light;
	light.name = lightInfo.name;
	light.color = glm::vec3{ lightInfo.r, lightInfo.g, lightInfo.b };
	light.intensity = lightInfo.intensity;

	debug("received pointLight: { name = ",
		light.name,
		", color = ",
		glm::to_string(light.color),
		", intensity = ",
		light.intensity,
		" }");

	if (std::find_if(
		    resources.pointLights.begin(), resources.pointLights.end(), [name = light.name](const auto& light) {
			    return light.name == name;
		    }) != resources.pointLights.end()) {
		warn("Received the same PointLight two times: ", light.name);
	} else {
		resources.pointLights.emplace_back(light);
		info("Stored PointLight ", light.name);
	}

	return true;
}

bool receiveShader(socket_t socket,
	const uint8_t* buffer,
	std::size_t bufsize,
	/* out */ ClientTmpResources& resources)
{
	// Parse header
	const auto header = *reinterpret_cast<const ResourcePacket<shared::SpirvShaderInfo>*>(buffer);
	constexpr auto sizeOfHeader = sizeof(ResourcePacket<shared::SpirvShaderInfo>);
	const auto expectedSize = header.res.codeSizeInBytes;

	if (expectedSize > cfg::MAX_SHADER_SIZE) {
		err("Shader server sent is too big! (", expectedSize / 1024, " KiB)");
		return false;
	}

	const auto shadName = header.res.name;

	assert(static_cast<uint8_t>(header.res.stage) < static_cast<uint8_t>(shared::ShaderStage::UNKNOWN));

	// Retreive payload

	/** Obtain the memory to store the shader data in */
	void* shadCode = resources.allocator.alloc(expectedSize);
	if (!shadCode)
		return false;

	// Copy the first shader data embedded in the header packet into the shader memory area
	auto len = std::min(bufsize - sizeOfHeader, expectedSize);
	memcpy(shadCode, buffer + sizeOfHeader, len);

	// Receive remaining shader data as raw data packets (if needed)
	auto processedSize = len;
	while (processedSize < expectedSize) {
		const auto remainingSize = expectedSize - processedSize;
		assert(remainingSize > 0);

		len = std::min(remainingSize, bufsize);

		int bytesRead;
		// Receive the data directly into the shader memory area (avoids a memcpy from the buffer)
		if (!receivePacket(socket, reinterpret_cast<uint8_t*>(shadCode) + processedSize, len, &bytesRead)) {
			resources.allocator.deallocLatest();
			return false;
		}

		processedSize += bytesRead;
	}

	if (processedSize != expectedSize) {
		warn("Processed more bytes than expected!");
	}

	shared::SpirvShader shader;
	shader.codeSizeInBytes = expectedSize;
	shader.code = reinterpret_cast<uint32_t*>(shadCode);
	shader.passNumber = header.res.passNumber;
	shader.stage = header.res.stage;

	if (resources.shaders.count(shadName) > 0) {
		warn("Received the same shader two times: ", shadName);
	} else {
		resources.shaders[shadName] = shader;
		info("Stored shader ", shadName);
	}

	info("Received shader ", shadName, ": ", shader.codeSizeInBytes, " B");
	if (gDebugLv >= LOGLV_VERBOSE) {
		dumpBytes(shader.code, shader.codeSizeInBytes);
	}

	return true;
}
