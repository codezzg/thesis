#include "client.hpp"
#include "buffers.hpp"
#include "clock.hpp"
#include "config.hpp"
#include "defer.hpp"
#include "frame_utils.hpp"
#include "hashing.hpp"
#include "logging.hpp"
#include "multipass.hpp"
#include "pipelines.hpp"
#include "profile.hpp"
#include "renderpass.hpp"
#include "shader_data.hpp"
#include "textures.hpp"
#include "transform.hpp"
#include "udp_messages.hpp"
#include "units.hpp"
#include "window.hpp"
#include <algorithm>
#include <future>
#include <set>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

using namespace logging;

constexpr auto RENDER_FRAME_TIME = std::chrono::milliseconds{ 16 };
constexpr auto SERVER_UPDATE_TIME = std::chrono::milliseconds{ 33 };

extern bool gUseCamera;
extern bool gLimitFrameTime;

void VulkanClient::run(const char* ip)
{
	app.init();

	glfwSetWindowUserPointer(app.window, this);
	if (gUseCamera) {
		glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		glfwSetCursorPosCallback(app.window, cbCursorMoved);
	}
	glfwSetKeyCallback(app.window, cbKeyPressed);

	if (!connectToServer(ip))
		return;

	measure_ms("Init Vulkan", LOGLV_INFO, [this]() { initVulkan(); });

	mainLoop();
	cleanup();
}

void VulkanClient::disconnect()
{
	sendTCPMsg(endpoints.reliable.socket, TcpMsgType::DISCONNECT);
}

void VulkanClient::initVulkan()
{
	stagingBuffer = createStagingBuffer(app, megabytes(256));
	{
		createPermanentBuffers(stagingBuffer);

		// Create default textures
		TextureLoader texLoader{ stagingBuffer };
		std::vector<std::future<bool>> texLoadTasks;
		texLoadTasks.emplace_back(texLoader.addTextureAsync(
			netRsrc.defaults.diffuseTex, "textures/default.jpg", shared::TextureFormat::RGBA));
		texLoadTasks.emplace_back(texLoader.addTextureAsync(
			netRsrc.defaults.specularTex, "textures/default_spec.jpg", shared::TextureFormat::GREY));
		texLoadTasks.emplace_back(texLoader.addTextureAsync(
			netRsrc.defaults.normalTex, "textures/default_norm.jpg", shared::TextureFormat::RGBA));
		for (auto& res : texLoadTasks) {
			if (!res.get()) {
				err("Failed to load texture image! Latest error: ", texLoader.getLatestError());
			}
		}
		texLoader.create(app);
	}

	{
		// Create default materials
		shared::Material dfltMat = { SID_NONE, SID_NONE, SID_NONE, SID_NONE };
		netRsrc.defaults.material = createMaterial(dfltMat, netRsrc);
	}

	app.texSampler = createTextureSampler(app);
	// app.cubeSampler = createTextureCubeSampler(app);
	// loadSkybox();

	// Create basic Vulkan resources
	app.swapChain = createSwapChain(app);
	app.swapChain.imageViews = createSwapChainImageViews(app, app.swapChain);

	app.renderPass = createMultipassRenderPass(app);

	app.gBuffer.createAttachments(app);

	app.swapChain.depthImage = createDepthImage(app);
	app.swapChain.framebuffers = createSwapChainMultipassFramebuffers(app, app.swapChain);
	app.commandBuffers = createSwapChainCommandBuffers(app, app.commandPool);
	app.pipelineCache = createPipelineCache(app);

	app.descriptorPool = createDescriptorPool(app);

	// Initialize resource maps
	app.res.init(app.device, app.descriptorPool);

	// Create pipelines
	const auto descSetLayouts = createMultipassDescriptorSetLayouts(app);
	app.res.descriptorSetLayouts->add("view_res", descSetLayouts[0]);
	app.res.descriptorSetLayouts->add("gbuffer_res", descSetLayouts[1]);
	app.res.descriptorSetLayouts->add("mat_res", descSetLayouts[2]);
	app.res.descriptorSetLayouts->add("obj_res", descSetLayouts[3]);

	app.res.pipelineLayouts->add("multi", createPipelineLayout(app, descSetLayouts));

	const auto pipelines = createPipelines(app, netRsrc.shaders);
	app.res.pipelines->add("gbuffer", pipelines[0]);
	app.res.pipelines->add("skybox", pipelines[1]);
	app.res.pipelines->add("swap", pipelines[2]);
	// app.gBuffer.pipeline = pipelines[0];
	// app.skybox.pipeline = pipelines[1];
	// app.swapChain.pipeline = pipelines[2];

	createPermanentDescriptorSets();

	recordAllCommandBuffers();

	createSemaphores();

	prepareCamera();
}

void VulkanClient::startUdp(const char* serverIp)
{
	debug("Starting passive EP...");
	endpoints.passive =
		startEndpoint("0.0.0.0", cfg::UDP_SERVER_TO_CLIENT_PORT, Endpoint::Type::PASSIVE, SOCK_DGRAM);
	networkThreads.udpPassive = std::make_unique<UdpPassiveThread>(endpoints.passive);

	debug("Starting active EP towards ", serverIp, ":", cfg::UDP_CLIENT_TO_SERVER_PORT, " ...");
	endpoints.active = startEndpoint(serverIp, cfg::UDP_CLIENT_TO_SERVER_PORT, Endpoint::Type::ACTIVE, SOCK_DGRAM);
	networkThreads.udpActive = std::make_unique<UdpActiveThread>(endpoints.active);

	updateReqs.reserve(256);
}

bool VulkanClient::connectToServer(const char* serverIp)
{
	endpoints.reliable = startEndpoint(serverIp, cfg::RELIABLE_PORT, Endpoint::Type::ACTIVE, SOCK_STREAM);

	debug(":: Performing handshake");
	if (!tcp_performHandshake(endpoints.reliable.socket)) {
		err("Failed to perform handshake.");
		return false;
	}

	debug(":: Starting UDP endpoints...");
	startUdp(serverIp);

	debug(":: Sending READY...");
	if (!tcp_sendReadyAndWait(endpoints.reliable.socket)) {
		err("Failed to send or receive READY.");
		return false;
	}
	debug(":: Received READY.");

	debug(":: Starting TCP listening loop");
	networkThreads.keepalive = std::make_unique<KeepaliveThread>(endpoints.reliable.socket);
	networkThreads.tcpMsg = std::make_unique<TcpMsgThread>(endpoints.reliable);

	// Ready to start the main loop

	return true;
}

void VulkanClient::checkAssets(const ClientTmpResources& resources)
{
	// Collect textures ids into a set (so we can use set_difference later)
	std::set<StringId> textureSet;
	for (const auto& pair : resources.textures)
		textureSet.emplace(pair.first);

	for (const auto& model : resources.models) {

		std::set<StringId> neededTextureSet;

		// Check materials, and gather needed textures information in the meantime
		for (const auto& matName : model.materials) {
			if (matName == SID_NONE)
				continue;
			auto it = std::find_if(resources.materials.begin(),
				resources.materials.end(),
				[matName](const auto& mat) { return mat.name == matName; });
			if (it != resources.materials.end()) {
				neededTextureSet.emplace(it->diffuseTex);
				neededTextureSet.emplace(it->specularTex);
			} else {
				warn("Material ",
					matName,
					" is needed by model ",
					model.name,
					" but was not received!");
			}
		}

		neededTextureSet.erase(SID_NONE);

		// Find if we're missing some textures
		std::set<StringId> diffTextureSet;
		std::set_difference(neededTextureSet.begin(),
			neededTextureSet.end(),
			textureSet.begin(),
			textureSet.end(),
			std::inserter(diffTextureSet, diffTextureSet.begin()));

		for (const auto& tex : diffTextureSet)
			warn("Texture ", tex, " is needed by model ", model.name, " but was not received!");
	}
}

bool VulkanClient::loadAssets(const ClientTmpResources& resources,
	std::vector<ModelInfo>& newModels,
	std::vector<Material>& newMaterials)
{
	newModels.reserve(resources.models.size());
	newMaterials.reserve(resources.materials.size());

	// Save models into permanent storage
	netRsrc.models.insert(netRsrc.models.end(), resources.models.begin(), resources.models.end());
	for (const auto& model : resources.models) {
		if (objTransforms.find(model.name) != objTransforms.end()) {
			warn("Received model ", model.name, " more than once: ignoring.");
			continue;
		}
		objTransforms[model.name] = glm::mat4{ 1.f };
		newModels.emplace_back(model);
	}

	// Save lights into permanent storage
	netRsrc.pointLights.insert(
		netRsrc.pointLights.end(), resources.pointLights.begin(), resources.pointLights.end());
	for (const auto& light : resources.pointLights) {
		if (objTransforms.find(light.name) != objTransforms.end()) {
			warn("Received light ", light.name, " more than once: ignoring.");
			continue;
		}
		objTransforms[light.name] = glm::mat4{ 1.f };
	}

	{
		// Save shaders into permanent storage
		netRsrc.shaders.reserve(resources.shaders.size());
		std::size_t shaderMemNeeded = 0;
		for (const auto& pair : resources.shaders) {
			// TODO right now we don't check if we receive the same shader two times.
			const auto& shader = pair.second;
			netRsrc.shaders.emplace_back(shader);
			shaderMemNeeded += shader.codeSizeInBytes;
		}

		// Copy the shader code from temporary to permanent area
		netRsrc.shadersCode.reserve(shaderMemNeeded);
		std::size_t shaderMemOffset = 0;
		for (auto& shader : netRsrc.shaders) {
			memcpy(netRsrc.shadersCode.data() + shaderMemOffset, shader.code, shader.codeSizeInBytes);
			// Update the pointer
			shader.code = reinterpret_cast<uint32_t*>(netRsrc.shadersCode.data() + shaderMemOffset);
			shaderMemOffset += shader.codeSizeInBytes;
		}
	}

	{
		/// Load textures
		TextureLoader texLoader{ stagingBuffer };
		std::vector<std::future<bool>> texLoadTasks;
		texLoadTasks.reserve(resources.textures.size());

		// Create textures received from server
		for (const auto& pair : resources.textures) {
			if (pair.first == SID_NONE)
				continue;
			texLoadTasks.emplace_back(texLoader.addTextureAsync(netRsrc.textures[pair.first], pair.second));
		}

		for (auto& res : texLoadTasks) {
			if (!res.get()) {
				err("Failed to load texture image! Latest error: ", texLoader.getLatestError());
				return false;
			}
		}

		texLoader.create(app);
	}

	// Convert shared::Materials to Materials we can use
	for (const auto& mat : resources.materials) {
		newMaterials.emplace_back(createMaterial(mat, netRsrc));
	}

	// Save materials into netRsrc
	netRsrc.materials.insert(netRsrc.materials.end(), newMaterials.begin(), newMaterials.end());

	return true;
}

void VulkanClient::mainLoop()
{
	FPSCounter fps;
	fps.start();

	updateObjectsUniformBuffer();
	updateViewUniformBuffer();
	updateLightsUniformBuffer();

	prepareReceivedGeomHashset();

	auto beginTime = std::chrono::high_resolution_clock::now();

	debug("Starting main loop");
	while (!glfwWindowShouldClose(app.window)) {
		LimitFrameTime lft{ RENDER_FRAME_TIME };
		lft.enabled = gLimitFrameTime;

		// Check if we disconnected
		if (!endpoints.reliable.connected) {
			warn("RelEP disconnected");
			break;
		}

		glfwPollEvents();

		runFrame();

		calcTimeStats(fps, beginTime);
	}

	// Close sockets
	info("closing endpoints.passive");
	closeEndpoint(endpoints.passive);
	info("closing endpoints.active");
	closeEndpoint(endpoints.active);
	info("closing endpoints.reliable");
	closeEndpoint(endpoints.reliable);

	info("waiting device idle");
	VLKCHECK(vkDeviceWaitIdle(app.device));
}

void VulkanClient::runFrame()
{
	// Receive network data
	updateReqs.clear();

	// Check for TCP messages
	if (networkThreads.tcpMsg->tryLockResources()) {
		const auto resources = networkThreads.tcpMsg->retreiveResources();

		// These are needed later, so save them
		std::vector<ModelInfo> newModels;
		std::vector<Material> newMaterials;
		checkAssets(*resources);
		loadAssets(*resources, newModels, newMaterials);

		networkThreads.tcpMsg->releaseResources();

		// Regenerate network-dependant data structures and resources
		recreateResources(newModels, newMaterials);
	}

	// Check for UDP messages
	measure_ms("receiveData", LOGLV_UBER_VERBOSE, [&]() {
		receiveData(*networkThreads.udpPassive, streamingBuffer, geometry, updateReqs, receivedGeomIds);
	});

	// Apply UDP update requests
	measure_ms("updateReq", LOGLV_UBER_VERBOSE, [&]() { applyUpdateRequests(); });

	// Enqueue acks to send (does not block if the mutex is not available yet)
	auto& acks = networkThreads.udpActive->acks;
	if (acksToSend.size() > 0 && acks.mtx.try_lock()) {
		debug("inserting ", acksToSend.size(), " acks");
		acks.list.insert(acks.list.end(), acksToSend.begin(), acksToSend.end());
		acks.mtx.unlock();
		acks.cv.notify_one();
		acksToSend.clear();
	}

	updateObjectsUniformBuffer();
	updateViewUniformBuffer();
	updateLightsUniformBuffer();

	cameraCtrl->processInput(app.window);

	drawFrame();
}

void VulkanClient::applyUpdateRequests()
{
	for (const auto& req : updateReqs) {
		switch (req.type) {
		case UpdateReq::Type::GEOM:
			updateModel(req.data.geom);
			acksToSend.emplace_back(req.data.geom.serialId);
			if (receivedGeomIds.load_factor() > 0.9) {
				receivedGeomIdsMemSize *= 2;
				receivedGeomIdsMem = realloc(receivedGeomIdsMem, receivedGeomIdsMemSize);
				receivedGeomIds = receivedGeomIds.copy(receivedGeomIdsMemSize, receivedGeomIdsMem);
				info("Reallocating receivedGeomIds. New size: ", receivedGeomIdsMemSize / 1024, " KiB");
			}
			receivedGeomIds.insert(req.data.geom.serialId, req.data.geom.serialId);
			break;
		case UpdateReq::Type::POINT_LIGHT:
			updatePointLight(req.data.pointLight, netRsrc);
			break;
		case UpdateReq::Type::TRANSFORM:
			updateTransform(req.data.transform, objTransforms);
			break;
		default:
			assert(false);
			break;
		}
	}
}

void VulkanClient::calcTimeStats(FPSCounter& fps,
	std::chrono::time_point<std::chrono::high_resolution_clock>& beginTime)
{
	auto& clock = Clock::instance();
	const auto endTime = std::chrono::high_resolution_clock::now();
	float dt = std::chrono::duration_cast<std::chrono::microseconds>(endTime - beginTime).count() / 1'000'000.f;
	if (dt > 1.f)
		dt = clock.targetDeltaTime;
	clock.update(dt);
	beginTime = endTime;

	fps.addFrame();
	fps.report();
}

void VulkanClient::recreateResources(const std::vector<ModelInfo>& newModels, const std::vector<Material>& newMaterials)
{
	if (newModels.size() > 0) {
		info("Updating geometry buffers");
		updateGeometryBuffers(app, geometry, newModels);
	}

	info("Updating uniform buffers");
	// Create new UBOs for new models
	for (const auto& model : newModels) {
		uniformBuffers.addBuffer(model.name, sizeof(ObjectUBO));
	}

	// Create new descriptor sets for new materials and models
	if (newModels.size() + newMaterials.size() > 0) {
		info("Updating descriptor sets");
		auto descriptorSets = createMultipassTransitoryDescriptorSets(
			app, uniformBuffers, newMaterials, newModels, app.texSampler, app.cubeSampler);
		assert(descriptorSets.size() == newModels.size() + newMaterials.size());

		// One descriptor set per material
		for (unsigned i = 0; i < newMaterials.size(); ++i) {
			app.res.descriptorSets->add(newMaterials[i].name, descriptorSets[i]);
		}
		// One descriptor set per object (model)
		for (unsigned i = 0; i < newModels.size(); ++i) {
			app.res.descriptorSets->add(newModels[i].name, descriptorSets[newMaterials.size() + i]);
		}
	}
	recreateSwapChain();
}

void VulkanClient::recreateSwapChain()
{
	warn("Recreating swap chain");
	int width, height;
	glfwGetWindowSize(app.window, &width, &height);
	if (width == 0 || height == 0)
		return;

	VLKCHECK(vkDeviceWaitIdle(app.device));

	cleanupSwapChain();

	// TODO: pass old swapchain?
	app.swapChain = createSwapChain(app);
	app.swapChain.imageViews = createSwapChainImageViews(app, app.swapChain);
	app.swapChain.depthImage = createDepthImage(app);
	app.gBuffer.createAttachments(app);
	// app.renderPass = createMultipassRenderPass(app);

	updateGBufferDescriptors(app, app.res.descriptorSets->get("gbuffer_res"), app.texSampler);

	app.swapChain.framebuffers = createSwapChainMultipassFramebuffers(app, app.swapChain);

	recordAllCommandBuffers();
	updateObjectsUniformBuffer();
	updateViewUniformBuffer();
	updateLightsUniformBuffer();
}

void VulkanClient::createSemaphores()
{
	imageAvailableSemaphore = app.res.semaphores->create("image_available");
	renderFinishedSemaphore = app.res.semaphores->create("render_finished");
}

void VulkanClient::drawFrame()
{
	uint32_t imageIndex;
	if (!acquireNextSwapImage(app, imageAvailableSemaphore, imageIndex)) {
		info("Recreating swap chain");
		recreateSwapChain();
		return;
	}

	renderFrame(imageIndex);
	submitFrame(imageIndex);
}

void VulkanClient::renderFrame(uint32_t imageIndex)
{
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	// Wait for image
	const std::array<VkPipelineStageFlags, 1> waitStages = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
	submitInfo.pWaitDstStageMask = waitStages.data();

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &app.commandBuffers[imageIndex];

	// Signal semaphore when done
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

	VLKCHECK(vkQueueSubmit(app.queues.graphics, 1, &submitInfo, VK_NULL_HANDLE));
}

void VulkanClient::submitFrame(uint32_t imageIndex)
{
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
	const std::array<VkSwapchainKHR, 1> swapChains = { app.swapChain.handle };
	presentInfo.swapchainCount = swapChains.size();
	presentInfo.pSwapchains = swapChains.data();
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pResults = nullptr;

	const auto result = vkQueuePresentKHR(app.queues.present, &presentInfo);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		info("Swap chain out of date or suboptimal: recreating");
		recreateSwapChain();
	} else if (result != VK_SUCCESS)
		throw std::runtime_error("failed to present swap chain image!");

	VLKCHECK(vkQueueWaitIdle(app.queues.graphics));
}

void VulkanClient::updateObjectsUniformBuffer()
{
	static auto startTime = std::chrono::high_resolution_clock::now();

	for (const auto& model : netRsrc.models) {
		auto objBuf = uniformBuffers.getBuffer(model.name);
		assert(objBuf && objBuf->ptr && objBuf->size >= sizeof(ObjectUBO));

		auto ubo = reinterpret_cast<ObjectUBO*>(objBuf->ptr);

		if (gUseCamera) {
			ubo->model = glm::mat4{ 1.f };   // objTransforms[model.name];
							 // verbose("filling ",
							 // ubo,
							 //" / ",
							 // std::hex,
			//(uintptr_t)((uint8_t*)ubo + sizeof(ObjectUniformBufferObject)),
			//"  with transform ",
			// glm::to_string(ubo->model));
		} else {
			auto currentTime = std::chrono::high_resolution_clock::now();
			float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime)
					     .count();
			ubo->model = glm::rotate(glm::mat4{ 1.0f },
				(time + model.name % 259) * glm::radians(89.f),
				glm::vec3{ 0.f, -1.f, 0.f });
		}
	}
}

void VulkanClient::updateViewUniformBuffer()
{
	auto viewBuf = uniformBuffers.getBuffer(sid("view"));
	assert(viewBuf && viewBuf->ptr && viewBuf->size >= sizeof(ViewUBO));

	auto ubo = reinterpret_cast<ViewUBO*>(viewBuf->ptr);

	const auto view = camera.viewMatrix();
	auto proj = glm::perspective(
		glm::radians(60.f), app.swapChain.extent.width / float(app.swapChain.extent.height), 0.1f, 300.f);
	// Flip y
	proj[1][1] *= -1;
	ubo->viewProj = proj * view;
	ubo->viewPos = glm::vec3{ camera.position.x, camera.position.y, camera.position.z };
	ubo->opts = shaderOpts.getRepr();
	uberverbose("viewPos = ", glm::to_string(ubo->viewPos));
}

void VulkanClient::updateLightsUniformBuffer()
{
	auto lightBuf = uniformBuffers.getBuffer(sid("lights"));
	assert(lightBuf && lightBuf->ptr && lightBuf->size >= sizeof(LightsUBO));

	auto ubo = reinterpret_cast<LightsUBO*>(lightBuf->ptr);

	// FIXME
	assert(netRsrc.pointLights.size() <= LightsUBO::MAX_LIGHTS);
	ubo->nPointLights = netRsrc.pointLights.size();
	for (unsigned i = 0; i < netRsrc.pointLights.size(); ++i) {
		const auto& pl = netRsrc.pointLights[i];
		const auto& plt = Transform::fromMatrix(objTransforms[pl.name]);
		ubo->pointLights[i] =
			UboPointLight{ plt.getPosition(), pl.attenuation, { pl.color.r, pl.color.g, pl.color.b }, 0 };
	}
}

void VulkanClient::createPermanentBuffers(Buffer& stagingBuffer)
{
	{
		// Create uniform buffers.
		// We store all possible uniform buffers inside as little actual Buffers as possible via a BufferArray.
		// Then we use descriptors of type UNIFORM_BUFFER_DYNAMIC with the subBuffers' bufOffset
		// as dynamic offset.
		const auto uboSize = sizeof(ViewUBO) + sizeof(LightsUBO) + 10 * sizeof(ObjectUBO);
		uniformBuffers.initialize(app, findMaxUboRange(app.physicalDevice));
		uniformBuffers.reserve(uboSize);
		uniformBuffers.mapAllBuffers();

		// Add the View UBO once
		uniformBuffers.addBuffer(sid("view"), sizeof(ViewUBO));

		// Add the Lights UBO once
		uniformBuffers.addBuffer(sid("lights"), sizeof(LightsUBO));
	}

	BufferAllocator bufAllocator;

	// screen quad buffer
	bufAllocator.addBuffer(app.screenQuadBuffer, getScreenQuadBufferProperties());

	// skybox buffer
	// bufAllocator.addBuffer(app.skybox.buffer, getSkyboxBufferProperties());

	// Create initial buffers for geometry
	bufAllocator.addBuffer(geometry.vertexBuffer,
		8192 * sizeof(Vertex),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	bufAllocator.addBuffer(geometry.indexBuffer,
		32768 * sizeof(Index),
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	bufAllocator.create(app);

	// Bind memory for geometry buffers
	mapBuffersMemory(app.device, { &geometry.vertexBuffer, &geometry.indexBuffer });
	memset(geometry.vertexBuffer.ptr, 0, geometry.vertexBuffer.size);
	memset(geometry.indexBuffer.ptr, 0, geometry.indexBuffer.size);

	// Allocate enough memory to contain all vertices and indices
	streamingBuffer.resize(megabytes(128));

	if (!fillScreenQuadBuffer(app, app.screenQuadBuffer, stagingBuffer))
		throw std::runtime_error("Failed to create screenQuadBuffer!");

	// const auto off = fillSkyboxBuffer(app, app.skybox.buffer, stagingBuffer);
	// if (off < 0)
	// throw std::runtime_error("Failed to create skybox buffer!");
	// app.skybox.indexOff = static_cast<VkDeviceSize>(off);
}

void VulkanClient::prepareCamera()
{
	// Prepare camera
	camera.position = glm::vec3{ -7, 13, 12 };
	camera.yaw = -60;
	camera.pitch = -13;
	if (gUseCamera)
		cameraCtrl = std::make_unique<FPSCameraController>(camera);
	else
		cameraCtrl = std::make_unique<CubeCameraController>(camera);
}

void VulkanClient::loadSkybox()
{
	// TODO this may be allocated with the other textures, but for now let's keep it
	// separate to keep the code cleaner
	measure_ms("Load Skybox", LOGLV_INFO, [this]() {
		app.skybox.image = createSkybox(app);
		if (app.skybox.image.handle == VK_NULL_HANDLE) {
			throw std::runtime_error("Failed to load skybox");
		}
		assert(app.skybox.image.memory != VK_NULL_HANDLE);
	});
}

void VulkanClient::recordAllCommandBuffers()
{
	info("recording cmd buffers with ", netRsrc.models.size(), " models");
	recordMultipassCommandBuffers(app, app.commandBuffers, geometry, netRsrc, uniformBuffers);
}

void VulkanClient::createPermanentDescriptorSets()
{
	// Create the permanent descriptor sets. This won't ever be recreated.
	auto descriptorSets = createMultipassPermanentDescriptorSets(app, uniformBuffers, app.texSampler);

	//// Store them into app resources
	// A descriptor set for the view-dependant stuff
	app.res.descriptorSets->add("view_res", descriptorSets[0]);
	// One for the shader-dependant stuff
	app.res.descriptorSets->add("gbuffer_res", descriptorSets[1]);
}

void VulkanClient::cleanupSwapChain()
{
	// Destroy the gbuffer and all its attachments
	app.gBuffer.destroy(app.device);
	// Destroy the swapchain and all its images and framebuffers
	app.swapChain.destroy(app.device);

	vkResetCommandPool(app.device, app.commandPool, 0);
}

void VulkanClient::cleanup()
{
	cleanupSwapChain();

	unmapBuffersMemory(app.device,
		{
			geometry.vertexBuffer,
			geometry.indexBuffer,
		});
	uniformBuffers.unmapAllBuffers();

	vkDestroySampler(app.device, app.texSampler, nullptr);
	// vkDestroySampler(app.device, app.cubeSampler, nullptr);

	{
		std::vector<Image> imagesToDestroy;
		imagesToDestroy.reserve(2 + netRsrc.textures.size());
		imagesToDestroy.emplace_back(netRsrc.defaults.diffuseTex);
		imagesToDestroy.emplace_back(netRsrc.defaults.specularTex);
		imagesToDestroy.emplace_back(netRsrc.defaults.normalTex);
		for (const auto& tex : netRsrc.textures)
			imagesToDestroy.emplace_back(tex.second);
		destroyAllImages(app.device, imagesToDestroy);
	}
	// destroyImage(app.device, app.skybox.image);

	{
		std::vector<Buffer> buffersToDestroy;
		buffersToDestroy.emplace_back(app.screenQuadBuffer);
		buffersToDestroy.emplace_back(geometry.vertexBuffer);
		buffersToDestroy.emplace_back(geometry.indexBuffer);
		destroyAllBuffers(app.device, buffersToDestroy);
	}
	uniformBuffers.cleanup();

	destroyBuffer(app.device, stagingBuffer);

	vkDestroyPipelineCache(app.device, app.pipelineCache, nullptr);
	vkDestroyRenderPass(app.device, app.renderPass, nullptr);

	free(receivedGeomIdsMem);

	app.res.cleanup();
	app.cleanup();
}

void VulkanClient::prepareReceivedGeomHashset()
{
	// Prepare the memory for the set of received geomUpdates serials.
	// Initially use a number of elements of 2 * [(total vertices we expect) / (max vertices per chunk) +
	//				(total indices we expect) / (max indices per chunk)]
	constexpr auto payloadSize = UdpPacket().payload.size();
	const auto maxVerticesPerPayload = (payloadSize - sizeof(GeomUpdateHeader)) / sizeof(Vertex);
	const auto maxIndicesPerPayload = (payloadSize - sizeof(GeomUpdateHeader)) / sizeof(Index);
	const auto expectedVertices = 300'000;
	const auto expectedIndices = 500'000;

	receivedGeomIdsMemSize = CF_HASHSET_GET_BUFFER_SIZE(
		uint32_t, 2 * (expectedVertices / maxVerticesPerPayload + expectedIndices / maxIndicesPerPayload));
	receivedGeomIdsMem = malloc(receivedGeomIdsMemSize);

	receivedGeomIds = cf::hashset<uint32_t>::create(receivedGeomIdsMemSize, receivedGeomIdsMem);
}

void VulkanClient::reqModel(uint16_t n)
{
	if (!endpoints.reliable.connected) {
		warn("Tried to send REQ_MODEL(", n, ") while endpoint is not connected");
		return;
	}

#pragma pack(push, 1)
	struct {
		TcpMsgType type;
		uint16_t payload;
	} msg;
#pragma pack(pop)
	msg.type = TcpMsgType::REQ_MODEL;
	msg.payload = n;

	if (!sendPacket(endpoints.reliable.socket, reinterpret_cast<uint8_t*>(&msg), sizeof(msg)))
		err("Failed to send REQ_MODEL(", n, ")");
}
