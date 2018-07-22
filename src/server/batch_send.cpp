#include "batch_send.hpp"
#include "endpoint.hpp"
#include "logging.hpp"
#include "server.hpp"
#include "server_resources.hpp"
#include "tcp_serialize.hpp"
#include "xplatform.hpp"

using namespace logging;

int64_t batch_sendTexture(socket_t clientSocket, Server& server, const std::string& texName, shared::TextureFormat fmt)
{
	if (texName.length() == 0)
		return 0;

	const auto texSid = sid(texName);
	if (server.stuffSent.has(texSid, texSid))
		return 0;

	info("* sending texture ", texName);

	std::size_t bytesSent;
	bool ok = sendTexture(clientSocket, server.resources, texName, fmt, &bytesSent);
	if (!ok) {
		err("batch_sendTexture: failed");
		return -1;
	}

	ok = expectTCPMsg(server, TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return -1;
	}

	server.stuffSent.insert(texSid, texSid);

	return static_cast<int64_t>(bytesSent);
}

/** Send material (along with textures used by it) */
static bool batch_sendMaterial(socket_t clientSocket,
	Server& server,
	/* inout */ std::unordered_set<std::pair<std::string, shared::TextureFormat>>& texturesToSend,
	const Material& mat)
{
	// Don't send the same material twice
	if (server.stuffSent.has(mat.name, mat.name))
		return true;

	debug("sending new material ", mat.name);

	bool ok = sendMaterial(clientSocket, mat);
	if (!ok) {
		err("Failed sending material");
		return false;
	}

	ok = expectTCPMsg(server, TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}

	// Send textures later, after geometry
	texturesToSend.emplace(mat.diffuseTex, shared::TextureFormat::RGBA);
	texturesToSend.emplace(mat.specularTex, shared::TextureFormat::GREY);
	texturesToSend.emplace(mat.normalTex, shared::TextureFormat::RGBA);

	server.stuffSent.insert(mat.name, mat.name);

	return true;
}

/** Send model (along with materials used by it) */
static bool batch_sendModel(socket_t clientSocket,
	Server& server,
	/* inout */ std::unordered_set<std::pair<std::string, shared::TextureFormat>>& texturesToSend,
	const Model& model)
{
	if (server.stuffSent.has(model.name, model.name))
		return true;

	bool ok = sendModel(clientSocket, model);
	if (!ok) {
		err("Failed sending model");
		return false;
	}

	ok = expectTCPMsg(server, TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}

	info("model.materials = ", model.data->materials.size());
	for (const auto& mat : model.data->materials) {
		if (!batch_sendMaterial(clientSocket, server, texturesToSend, mat))
			return false;
	}

	server.stuffSent.insert(model.name, model.name);

	return true;
}

static bool batch_sendShaders(socket_t clientSocket, Server& server, const char* baseName, uint8_t shaderStage)
{
	bool ok = sendShader(clientSocket,
		server.resources,
		(std::string{ baseName } + ".vert.spv").c_str(),
		shaderStage,
		shared::ShaderStage::VERTEX);
	if (!ok) {
		err("Failed sending shader");
		return false;
	}

	ok = expectTCPMsg(server, TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}

	ok = sendShader(clientSocket,
		server.resources,
		(std::string{ baseName } + ".frag.spv").c_str(),
		shaderStage,
		shared::ShaderStage::FRAGMENT);
	if (!ok) {
		err("Failed sending shader");
		return false;
	}

	ok = expectTCPMsg(server, TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}

	return true;
}

static bool batch_sendPointLight(socket_t clientSocket, Server& server, const shared::PointLight& light)
{
	if (server.stuffSent.has(light.name, light.name))
		return true;

	bool ok = sendPointLight(clientSocket, light);
	if (!ok) {
		err("Failed sending point light");
		return false;
	}
	ok = expectTCPMsg(server, TcpMsgType::RSRC_EXCHANGE_ACK);
	if (!ok) {
		warn("Not received RSRC_EXCHANGE_ACK!");
		return false;
	}

	server.stuffSent.insert(light.name, light.name);

	return true;
}

bool sendResourceBatch(socket_t clientSocket, Server& server, const ResourceBatch& batch, TexturesQueue& texturesQueue)
{
	std::unordered_set<std::pair<std::string, shared::TextureFormat>> texturesToSend;
	std::unordered_set<StringId> materialsSent;

	info("Sending ", batch.models.size(), " models");
	for (const auto& model : batch.models) {
		// This will also send dependent materials
		if (!batch_sendModel(clientSocket, server, texturesToSend, model))
			return false;

		texturesQueue.insert(texturesToSend.begin(), texturesToSend.end());

		// After sending model base info, schedule its geometry to be streamed and add it
		// to the scene (so its transform will be sent too)
		{
			std::lock_guard<std::mutex> lock{ server.toClient.modelsToSendMtx };
			server.toClient.modelsToSend.emplace_back(model);
		}

		auto node = server.scene.addNode(model.name, NodeType::MODEL, Transform{});
		// Make Sponza static (FIXME: ugly)
		if (node->name == sid((server.cwd + xplatPath("/models/sponza/sponza.dae")).c_str()))
			node->flags |= (1 << NODE_FLAG_STATIC);
	}

	// Send lights
	for (const auto& light : batch.pointLights) {
		if (!batch_sendPointLight(clientSocket, server, light))
			return false;
	}

	// Send shaders (and unload them immediately after)
	// const std::array<const char*, 3> shadersToSend = { "shaders/gbuffer", "shaders/skybox",
	// "shaders/composition"
	// }; for (unsigned i = 0; i < shadersToSend.size(); ++i) { if (!batch_sendShaders(clientSocket,
	// resources, shadersToSend[i], i)) return false;
	//}

	if (!sendTCPMsg(clientSocket, TcpMsgType::END_RSRC_EXCHANGE))
		return false;

	info("Done sending data");

	return true;
}
