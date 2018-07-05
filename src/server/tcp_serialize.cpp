#include "tcp_serialize.hpp"
#include "config.hpp"
#include "defer.hpp"
#include "logging.hpp"
#include "model.hpp"
#include "server_resources.hpp"
#include "tcp_messages.hpp"
#include <array>
#include <vector>

using namespace logging;

bool sendMaterial(socket_t clientSocket, const Material& material)
{
	ResourcePacket<shared::Material> packet;
	packet.type = TcpMsgType::RSRC_TYPE_MATERIAL;
	packet.res.name = material.name;
	packet.res.diffuseTex = material.diffuseTex.length() > 0 ? sid(material.diffuseTex) : SID_NONE;
	packet.res.specularTex = material.specularTex.length() > 0 ? sid(material.specularTex) : SID_NONE;
	packet.res.normalTex = material.normalTex.length() > 0 ? sid(material.normalTex) : SID_NONE;

	debug("packet: { type = ",
		packet.type,
		", name = ",
		packet.res.name,
		" (",
		sidToString(packet.res.name),
		"), diffuse = ",
		packet.res.diffuseTex,
		", specular = ",
		packet.res.specularTex,
		", normal = ",
		packet.res.normalTex,
		" }");

	// We want to send this in a single packet. This is reasonable, as a packet should be at least
	// ~400 bytes of size and a material only takes some 10s.
	static_assert(sizeof(packet) <= cfg::PACKET_SIZE_BYTES, "One packet is too small to contain a material!");

	return sendPacket(
		clientSocket, reinterpret_cast<const uint8_t*>(&packet), sizeof(ResourcePacket<shared::Material>));
}

bool sendPointLight(socket_t clientSocket, const shared::PointLight& light)
{
	info("Sending point light ", light.name, " (", sidToString(light.name), ")");

	ResourcePacket<shared::PointLightInfo> packet;
	packet.type = TcpMsgType::RSRC_TYPE_POINT_LIGHT;
	packet.res.name = light.name;
	packet.res.r = light.color.r;
	packet.res.g = light.color.g;
	packet.res.b = light.color.b;
	packet.res.intensity = light.intensity;

	debug("packet: { type = ",
		packet.type,
		", name = ",
		packet.res.name,
		" (",
		sidToString(packet.res.name),
		"), color = ",
		light.color,
		", intensity = ",
		packet.res.intensity,
		" }");

	// We want to send this in a single packet. This is reasonable, as a packet should be at least
	// ~400 bytes of size and a point light only takes some 10s.
	static_assert(sizeof(packet) <= cfg::PACKET_SIZE_BYTES, "One packet is too small to contain a point light!");

	return sendPacket(clientSocket,
		reinterpret_cast<const uint8_t*>(&packet),
		sizeof(ResourcePacket<shared::PointLightInfo>));
}

bool sendModel(socket_t clientSocket, const Model& model)
{
	info("Sending model ", model.name, " (", sidToString(model.name), ")");

	std::array<uint8_t, cfg::PACKET_SIZE_BYTES> packet;

	// Prepare header
	ResourcePacket<shared::Model> header;
	header.type = TcpMsgType::RSRC_TYPE_MODEL;
	header.res.name = model.name;
	header.res.nVertices = model.nVertices;
	header.res.nIndices = model.nIndices;
	header.res.nMaterials = model.materials.size();
	header.res.nMeshes = model.meshes.size();

	// Put header into packet
	debug("header: { type = ",
		header.type,
		", name = ",
		header.res.name,
		", nVertices = ",
		header.res.nVertices,
		", nIndices = ",
		header.res.nIndices,
		", nMaterials = ",
		int(header.res.nMaterials),
		", nMeshes = ",
		int(header.res.nMeshes),
		" }");
	constexpr auto sizeOfHeader = sizeof(ResourcePacket<shared::Model>);
	memcpy(packet.data(), reinterpret_cast<const uint8_t*>(&header), sizeOfHeader);

	const auto matSize = header.res.nMaterials * sizeof(StringId);
	const auto meshSize = header.res.nMeshes * sizeof(shared::Mesh);
	const auto size = matSize + meshSize;

	// Fill remaining space with payload (materials | meshes)
	std::vector<uint8_t> payload(size);
	for (unsigned i = 0; i < model.materials.size(); ++i) {
		// For materials we just copy the name
		reinterpret_cast<StringId*>(payload.data())[i] = model.materials[i].name;
	}
	memcpy(payload.data() + matSize, model.meshes.data(), meshSize);

	auto len = std::min(size, packet.size() - sizeOfHeader);
	memcpy(packet.data() + sizeOfHeader, payload.data(), len);

	if (!sendPacket(clientSocket, packet.data(), len + sizeOfHeader))
		return false;

	std::size_t bytesSent = len;
	// Send more packets with remaining payload if needed
	while (bytesSent < size) {
		auto len = std::min(size - bytesSent, cfg::PACKET_SIZE_BYTES);
		if (!sendPacket(clientSocket, reinterpret_cast<const uint8_t*>(payload.data()) + bytesSent, len))
			return false;
		bytesSent += len;
	}

	return true;
}

bool sendTexture(socket_t clientSocket,
	ServerResources& resources,
	const std::string& texName,
	shared::TextureFormat format)
{
	using shared::TextureInfo;

	std::array<uint8_t, cfg::PACKET_SIZE_BYTES> packet;

	// Load the texture, and unload it as we finished using it
	resources.loadTexture(texName.c_str());
	DEFER([&resources]() {
		resources.textures.clear();
		resources.allocator.deallocLatest();
	});

	// Prepare header
	const auto texNameSid = sid(texName);
	const auto& texture = resources.textures[texNameSid];
	ResourcePacket<TextureInfo> header;
	header.type = TcpMsgType::RSRC_TYPE_TEXTURE;
	header.res.name = texNameSid;
	header.res.format = format;
	header.res.size = texture.size;

	info("Sending texture ", texName, " (", texNameSid, ")");

	// Put header into packet
	debug("texheader: { type = ",
		header.type,
		", size = ",
		header.res.size,
		", name = ",
		header.res.name,
		", format = ",
		int(header.res.format),
		" }");
	constexpr auto sizeOfHeader = sizeof(ResourcePacket<TextureInfo>);
	memcpy(packet.data(), reinterpret_cast<const uint8_t*>(&header), sizeOfHeader);

	// Fill remaining space with payload
	auto len = std::min(texture.size, packet.size() - sizeOfHeader);
	memcpy(packet.data() + sizeOfHeader, texture.data, len);

	if (!sendPacket(clientSocket, packet.data(), len + sizeOfHeader))
		return false;

	std::size_t bytesSent = len;
	// Send more packets with remaining payload if needed
	while (bytesSent < texture.size) {
		auto len = std::min(texture.size - bytesSent, cfg::PACKET_SIZE_BYTES);
		if (!sendPacket(clientSocket, reinterpret_cast<const uint8_t*>(texture.data) + bytesSent, len))
			return false;
		bytesSent += len;
	}

	return true;
}

bool sendShader(socket_t clientSocket,
	ServerResources& resources,
	const char* shadName,
	uint8_t passNumber,
	shared::ShaderStage stage)
{
	using shared::SpirvShaderInfo;

	std::array<uint8_t, cfg::PACKET_SIZE_BYTES> packet;

	// Load the shader, and unload it as we finished using it
	resources.loadShader(shadName);
	DEFER([&resources]() {
		resources.shaders.clear();
		resources.allocator.deallocLatest();
	});

	// Prepare header
	const auto shadNameSid = sid(shadName);
	const auto& shader = resources.shaders[shadNameSid];
	ResourcePacket<SpirvShaderInfo> header;
	header.type = TcpMsgType::RSRC_TYPE_SHADER;
	header.res.name = shadNameSid;
	header.res.passNumber = passNumber;
	header.res.stage = stage;
	header.res.codeSizeInBytes = shader.codeSizeInBytes;

	info("Sending shader ", shadName, " (", shadNameSid, ")");

	// Put header into packet
	debug("shadheader: { type = ",
		header.type,
		", size = ",
		header.res.codeSizeInBytes,
		", name = ",
		header.res.name,
		", passNumber = ",
		int(header.res.passNumber),
		", stage = ",
		int(header.res.stage),
		" }");
	constexpr auto sizeOfHeader = sizeof(ResourcePacket<SpirvShaderInfo>);
	memcpy(packet.data(), reinterpret_cast<const uint8_t*>(&header), sizeOfHeader);

	// Fill remaining space with payload
	auto len = std::min(shader.codeSizeInBytes, packet.size() - sizeOfHeader);
	memcpy(packet.data() + sizeOfHeader, shader.code, len);

	if (!sendPacket(clientSocket, packet.data(), len + sizeOfHeader))
		return false;

	std::size_t bytesSent = len;
	// Send more packets with remaining payload if needed
	while (bytesSent < shader.codeSizeInBytes) {
		auto len = std::min(shader.codeSizeInBytes - bytesSent, cfg::PACKET_SIZE_BYTES);
		if (!sendPacket(clientSocket, reinterpret_cast<const uint8_t*>(shader.code) + bytesSent, len))
			return false;
		bytesSent += len;
	}

	return true;
}
