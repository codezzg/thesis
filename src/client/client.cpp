/** @author Giacomo Parolini, 2018 */
#include "FPSCounter.hpp"
#include "application.hpp"
#include "buffer_array.hpp"
#include "buffers.hpp"
#include "camera.hpp"
#include "camera_ctrl.hpp"
#include "client_endpoint.hpp"
#include "clock.hpp"
#include "commands.hpp"
#include "config.hpp"
#include "defer.hpp"
#include "formats.hpp"
#include "frame_data.hpp"
#include "frame_utils.hpp"
#include "gbuffer.hpp"
#include "geometry.hpp"
#include "images.hpp"
#include "logging.hpp"
#include "multipass.hpp"
#include "network_data.hpp"
#include "phys_device.hpp"
#include "pipelines.hpp"
#include "profile.hpp"
#include "renderpass.hpp"
#include "shader_opts.hpp"
#include "shared_resources.hpp"
#include "skybox.hpp"
#include "swap.hpp"
#include "textures.hpp"
#include "to_string.hpp"
#include "udp_messages.hpp"
#include "units.hpp"
#include "utils.hpp"
#include "validation.hpp"
#include "vertex.hpp"
#include "vulk_errors.hpp"
#include "vulk_utils.hpp"
#include "window.hpp"
#include "xplatform.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

// Fuck off, Windows
#undef max
#undef min

using namespace logging;
using namespace std::literals::string_literals;
using shared::TextureFormat;
using std::size_t;

constexpr auto RENDER_FRAME_TIME = std::chrono::milliseconds{ 16 };
constexpr auto SERVER_UPDATE_TIME = std::chrono::milliseconds{ 33 };

bool gUseCamera = true;
bool gLimitFrameTime = true;

class VulkanClient final {
public:
	void run(const char* ip)
	{
		app.init();

		glfwSetWindowUserPointer(app.window, this);
		// glfwSetWindowSizeCallback(app.window, onWindowResized);
		if (gUseCamera) {
			glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			glfwSetCursorPosCallback(app.window, cursorPosCallback);
		}
		glfwSetKeyCallback(app.window, keyCallback);

		if (!connectToServer(ip))
			return;

		measure_ms("Init Vulkan", LOGLV_INFO, [this]() { initVulkan(); });   // deferred rendering

		mainLoop(ip);
		cleanup();
	}

	void disconnect()
	{
		if (!relEP.disconnect())
			warn("Failed to disconnect!");
	}

private:
	Application app;

	bool fullscreen = false;

	ClientPassiveEndpoint passiveEP;
	ClientActiveEndpoint activeEP;
	ClientReliableEndpoint relEP;
	int64_t curFrame = -1;

	/** The semaphores are owned by `app.res`. We save their handles rather than querying them
	 *  each frame for performance reasons.
	 */
	VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;

	/** Struct containing geometry data (vertex/index buffers + metadata) */
	Geometry geometry;

	/** Single buffer containing all uniform buffer objects needed */
	BufferArray uniformBuffers{ VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT };

	/** Stores resources received via network */
	NetworkResources netRsrc;

	/** Memory area staging vertices and indices coming from the server */
	std::vector<uint8_t> streamingBuffer;

	Camera camera;
	std::unique_ptr<CameraController> cameraCtrl;

	/** Contains togglable debug options for shaders */
	ShaderOpts shaderOpts;

	void initVulkan()
	{
		// Create basic Vulkan resources
		app.swapChain = createSwapChain(app);
		app.swapChain.imageViews = createSwapChainImageViews(app, app.swapChain);

		app.renderPass = createMultipassRenderPass(app);

		app.gBuffer.createAttachments(app);

		app.swapChain.depthImage = createDepthImage(app);
		// app.swapChain.depthOnlyView = createImageView(app, app.swapChain.depthImage.handle,
		// formats::depth, VK_IMAGE_ASPECT_DEPTH_BIT);
		app.swapChain.framebuffers = createSwapChainMultipassFramebuffers(app, app.swapChain);
		app.commandBuffers = createSwapChainCommandBuffers(app, app.commandPool);
		app.pipelineCache = createPipelineCache(app);

		app.descriptorPool = createDescriptorPool(app, netRsrc);

		// Initialize resource maps
		app.res.init(app.device, app.descriptorPool);

		// Create pipelines
		const auto descSetLayouts = createMultipassDescriptorSetLayouts(app);
		app.res.descriptorSetLayouts->add("view_res", descSetLayouts[0]);
		app.res.descriptorSetLayouts->add("gbuffer_res", descSetLayouts[1]);
		app.res.descriptorSetLayouts->add("mat_res", descSetLayouts[2]);
		app.res.descriptorSetLayouts->add("obj_res", descSetLayouts[3]);

		app.res.pipelineLayouts->add("multi", createPipelineLayout(app, descSetLayouts));

		const auto pipelines = createPipelines(app);
		app.gBuffer.pipeline = pipelines[0];
		app.skybox.pipeline = pipelines[1];
		app.swapChain.pipeline = pipelines[2];

		createDescriptorSets();

		recordAllCommandBuffers();

		createSemaphores();

		prepareCamera();
	}

	void startNetwork(const char* serverIp)
	{
		debug("Starting passive EP...");
		passiveEP.startPassive("0.0.0.0", cfg::SERVER_TO_CLIENT_PORT, SOCK_DGRAM);
		passiveEP.runLoop();

		// activeEP.startActive(serverIp, cfg::CLIENT_TO_SERVER_PORT, SOCK_DGRAM);
		// activeEP.targetFrameTime = SERVER_UPDATE_TIME;
		// activeEP.runLoop();
	}

	bool connectToServer(const char* serverIp)
	{
		relEP.startActive(serverIp, cfg::RELIABLE_PORT, SOCK_STREAM);

		debug(":: Performing handshake");
		if (!relEP.performHandshake()) {
			err("Failed to perform handshake.");
			return false;
		}

		debug(":: Expecting START_RSRC_EXCHANGE");
		if (!relEP.expectStartResourceExchange()) {
			err("Didn't receive START_RSRC_EXCHANGE.");
			return false;
		}

		// Retreive one-time data from server
		{
			constexpr std::size_t ONE_TIME_DATA_BUFFER_SIZE = megabytes(64);
			ClientTmpResources resources{ ONE_TIME_DATA_BUFFER_SIZE };

			if (!relEP.sendRsrcExchangeAck()) {
				err("Failed to send RSRC_EXCHANGE_ACK");
				return false;
			}

			bool success = false;
			debug(":: Receiving one-time resources...");
			measure_ms("Recv Assets", LOGLV_INFO, [this, &resources, &success]() {
				success = relEP.receiveOneTimeData(resources);
			});
			if (!success) {
				err("Failed to receive one-time data.");
				return false;
			}

			// Process the received data
			debug(":: processing received resources...");
			measure_ms("Check Assets", LOGLV_INFO, [this, &resources]() { checkAssets(resources); });
			measure_ms("Load Assets", LOGLV_INFO, [this, &resources, &success]() {
				success = loadAssets(resources);
			});
			if (!success) {
				err("Failed to load assets.");
				return false;
			}

			// Drop the memory used for staging the resources as it's not needed anymore.
		}

		debug(":: Starting UDP endpoints...");
		startNetwork(serverIp);

		debug(":: Sending READY...");
		if (!relEP.sendReadyAndWait()) {
			err("Failed to send or receive READY.");
			return false;
		}
		debug(":: Received READY.");

		debug(":: Starting TCP listening loop");
		relEP.runLoop();

		// Ready to start the main loop

		return true;
	}

	/** Check we received all the resources needed by all models */
	void checkAssets(const ClientTmpResources& resources)
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

	bool loadAssets(const ClientTmpResources& resources)
	{
		constexpr VkDeviceSize STAGING_BUFFER_SIZE = megabytes(128);

		auto stagingBuffer = createStagingBuffer(app, STAGING_BUFFER_SIZE);
		DEFER([&]() {
			unmapBuffersMemory(app.device, { stagingBuffer });
			destroyBuffer(app.device, stagingBuffer);
		});

		loadSkybox();

		// Save models into permanent storage
		// NOTE: resources.models becomes invalid after this move
		netRsrc.models = std::move(resources.models);
		for (const auto& model : netRsrc.models)
			netRsrc.modelTransforms[model.name] = glm::mat4{ 1.f };

		// Save lights into permanent storage
		// TODO: do something more sophisticate to take advantage of the lights' dynMasks
		// NOTE: resources.pointLights becomes invalid after this move
		netRsrc.pointLights = std::move(resources.pointLights);

		{
			/// Load textures
			TextureLoader texLoader{ stagingBuffer };
			std::vector<std::future<bool>> texLoadTasks;
			texLoadTasks.reserve(3 + resources.textures.size());

			// Create default textures
			texLoadTasks.emplace_back(texLoader.addTextureAsync(
				netRsrc.defaults.diffuseTex, "textures/default.jpg", shared::TextureFormat::RGBA));
			texLoadTasks.emplace_back(texLoader.addTextureAsync(netRsrc.defaults.specularTex,
				"textures/default_spec.jpg",
				shared::TextureFormat::GREY));
			texLoadTasks.emplace_back(texLoader.addTextureAsync(
				netRsrc.defaults.normalTex, "textures/default_norm.jpg", shared::TextureFormat::RGBA));
			// Create textures received from server
			for (const auto& pair : resources.textures) {
				if (pair.first == SID_NONE)
					continue;
				texLoadTasks.emplace_back(
					texLoader.addTextureAsync(netRsrc.textures[pair.first], pair.second));
			}

			for (auto& res : texLoadTasks) {
				if (!res.get()) {
					err("Failed to load texture image! Latest error: ", texLoader.getLatestError());
					return false;
				}
			}

			texLoader.create(app);
			app.texSampler = createTextureSampler(app);
		}

		// Prepare materials
		{
			shared::Material dfltMat = { SID_NONE, SID_NONE, SID_NONE, SID_NONE };
			netRsrc.defaults.material = createMaterial(dfltMat, netRsrc);
		}
		for (const auto& mat : resources.materials) {
			netRsrc.materials.emplace_back(createMaterial(mat, netRsrc));
		}

		// Prepare buffers
		prepareBufferMemory(stagingBuffer);

		return true;
	}

	void mainLoop(const char* serverIp)
	{
		// startNetwork(serverIp);

		FPSCounter fps;
		fps.start();

		updateObjectsUniformBuffer();
		updateViewUniformBuffer();

		auto beginTime = std::chrono::high_resolution_clock::now();

		debug("Starting main loop");
		while (!glfwWindowShouldClose(app.window)) {
			LimitFrameTime lft{ RENDER_FRAME_TIME };
			lft.enabled = gLimitFrameTime;

			// Check if we disconnected
			if (!relEP.isConnected())
				break;

			glfwPollEvents();

			runFrame();

			calcTimeStats(fps, beginTime);
		}

		// Close sockets
		info("closing passiveEP");
		passiveEP.close();
		info("closing activeEP");
		activeEP.close();
		info("closing relEP");
		relEP.close();

		info("waiting device idle");
		VLKCHECK(vkDeviceWaitIdle(app.device));
	}

	void runFrame()
	{
		// Receive network data
		receiveData(passiveEP, streamingBuffer, geometry, netRsrc);
#ifndef NDEBUG
		if (gDebugLv >= LOGLV_VERBOSE) {
			dumpBytesIntoFileBin("dumps/sb.data", streamingBuffer.data(), streamingBuffer.size());
			dumpBytesIntoFileBin("dumps/vb.data",
				geometry.vertexBuffer.ptr,
				netRsrc.models.begin()->nVertices * sizeof(Vertex));
			dumpBytesIntoFileBin("dumps/ib.data",
				(uint8_t*)geometry.indexBuffer.ptr,
				netRsrc.models.begin()->nIndices * sizeof(Index));
		}

		for (const auto& model : netRsrc.models) {
			// Check max index is < vertices.size
			Index maxIdx = 0;
			const auto idxOff = geometry.locations[model.name].indexOff;
			for (unsigned i = 0; i < model.nIndices; ++i) {
				auto idx = reinterpret_cast<Index*>(
					reinterpret_cast<uint8_t*>(geometry.indexBuffer.ptr) + idxOff)[i];
				if (idx > maxIdx)
					maxIdx = idx;
			}
			verbose("max idx = ", maxIdx);
			assert(maxIdx < geometry.vertexBuffer.size / sizeof(Vertex));
		}
#endif

		updateObjectsUniformBuffer();
		updateViewUniformBuffer();

		cameraCtrl->processInput(app.window);

		drawFrame();
	}

	void calcTimeStats(FPSCounter& fps, std::chrono::time_point<std::chrono::high_resolution_clock>& beginTime)
	{
		auto& clock = Clock::instance();
		const auto endTime = std::chrono::high_resolution_clock::now();
		float dt = std::chrono::duration_cast<std::chrono::microseconds>(endTime - beginTime).count() /
			   1'000'000.f;
		if (dt > 1.f)
			dt = clock.targetDeltaTime;
		clock.update(dt);
		beginTime = endTime;

		fps.addFrame();
		fps.report();
	}

	void recreateSwapChain()
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
		app.renderPass = createMultipassRenderPass(app);

		updateGBufferDescriptors(app, app.res.descriptorSets->get("gbuffer_res"), app.texSampler);

		const auto pipelines = createPipelines(app);
		app.gBuffer.pipeline = pipelines[0];
		app.skybox.pipeline = pipelines[1];
		app.swapChain.pipeline = pipelines[2];

		app.swapChain.framebuffers = createSwapChainMultipassFramebuffers(app, app.swapChain);
		app.commandBuffers = createSwapChainCommandBuffers(app, app.commandPool);

		recordAllCommandBuffers();
		updateObjectsUniformBuffer();
		updateViewUniformBuffer();
	}

	void createSemaphores()
	{
		imageAvailableSemaphore = app.res.semaphores->create("image_available");
		renderFinishedSemaphore = app.res.semaphores->create("render_finished");
	}

	void drawFrame()
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

	void renderFrame(uint32_t imageIndex)
	{
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		// Wait for image
		const std::array<VkPipelineStageFlags, 1> waitStages = {
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		};
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

	void submitFrame(uint32_t imageIndex)
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

	void updateObjectsUniformBuffer()
	{
		static auto startTime = std::chrono::high_resolution_clock::now();

		for (const auto& model : netRsrc.models) {
			auto objBuf = uniformBuffers.getBuffer(model.name);
			assert(objBuf && objBuf->ptr && objBuf->size >= sizeof(ObjectUniformBufferObject));

			auto ubo = reinterpret_cast<ObjectUniformBufferObject*>(objBuf->ptr);

			if (gUseCamera) {
				// TODO
				ubo->model = netRsrc.modelTransforms[model.name];
				info("filling ", ubo, " with transform ", glm::to_string(ubo->model));
			} else {
				auto currentTime = std::chrono::high_resolution_clock::now();
				float time = std::chrono::duration<float, std::chrono::seconds::period>(
					currentTime - startTime)
						     .count();
				ubo->model = glm::rotate(glm::mat4{ 1.0f },
					(time + model.name % 259) * glm::radians(89.f),
					glm::vec3{ 0.f, -1.f, 0.f });
				// ubo->view = glm::lookAt(glm::vec3{ 14, 14, 14 },
				// glm::vec3{ 0, 0, 0 }, glm::vec3{ 0, 1, 0 });
			}
		}
	}

	void updateViewUniformBuffer()
	{
		auto viewBuf = uniformBuffers.getBuffer(sid("view"));
		assert(viewBuf && viewBuf->ptr && viewBuf->size >= sizeof(ViewUniformBufferObject));

		auto ubo = reinterpret_cast<ViewUniformBufferObject*>(viewBuf->ptr);

		ubo->view = camera.viewMatrix();
		// ubo->proj = camera.projMatrix();
		ubo->proj = glm::perspective(glm::radians(60.f),
			app.swapChain.extent.width / float(app.swapChain.extent.height),
			0.1f,
			300.f);
		// Flip y
		ubo->proj[1][1] *= -1;

		ubo->viewPos = glm::vec4{ camera.position.x, camera.position.y, camera.position.z, 0.f };
		if (netRsrc.pointLights.size() > 0) {
			const auto& pl = netRsrc.pointLights[0];
			ubo->pointLight = PointLight{ { pl.position.x, pl.position.y, pl.position.z, pl.intensity },
				{ pl.color.r, pl.color.g, pl.color.b, 0.0 } };
		}
		ubo->opts = shaderOpts.getRepr();
		uberverbose("viewPos = ", glm::to_string(ubo->viewPos));
	}

	void prepareBufferMemory(Buffer& stagingBuffer)
	{
		const auto uboSize =
			sizeof(ViewUniformBufferObject) + sizeof(ObjectUniformBufferObject) * netRsrc.models.size();
		uniformBuffers.initialize(app);
		uniformBuffers.reserve(uboSize);
		uniformBuffers.addBuffer(sid("view"), sizeof(ViewUniformBufferObject));
		for (const auto& model : netRsrc.models)
			uniformBuffers.addBuffer(model.name, sizeof(ObjectUniformBufferObject));

		// Create vertex, index and uniform buffers. These buffers are all created una-tantum.
		BufferAllocator bufAllocator;

		// schedule vertex/index buffer to be created and set the proper offsets into the
		// common buffer for all models
		geometry.locations = addVertexAndIndexBuffers(
			bufAllocator, geometry.vertexBuffer, geometry.indexBuffer, netRsrc.models);

		// uniform buffers
		// bufAllocator.addBuffer(uniformBuffers,
		// uboSize,
		// VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		// VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		// screen quad buffer
		bufAllocator.addBuffer(app.screenQuadBuffer, getScreenQuadBufferProperties());
		// skybox buffer
		bufAllocator.addBuffer(app.skybox.buffer, getSkyboxBufferProperties());

		bufAllocator.create(app);

		// Map device memory to host
		mapBuffersMemory(app.device,
			{
				&geometry.vertexBuffer,
				&geometry.indexBuffer,
			});
		uniformBuffers.mapAllBuffers();

		// Allocate enough memory to contain all vertices and indices
		streamingBuffer.resize(megabytes(16));
		// std::accumulate(
		// netRsrc.models.begin(), netRsrc.models.end(), 0, [](auto acc, const auto& pair) {
		// return acc + pair.second.nVertices + pair.second.nIndices;
		//}));

		if (!fillScreenQuadBuffer(app, app.screenQuadBuffer, stagingBuffer))
			throw std::runtime_error("Failed to create screenQuadBuffer!");

		const auto off = fillSkyboxBuffer(app, app.skybox.buffer, stagingBuffer);
		if (off < 0)
			throw std::runtime_error("Failed to create skybox buffer!");
		app.skybox.indexOff = static_cast<VkDeviceSize>(off);
	}

	void prepareCamera()
	{
		// Prepare camera
		camera.position = glm::vec3{ -7, 13, 12 };
		camera.yaw = -60;
		camera.pitch = -13;
		if (gUseCamera)
			cameraCtrl = std::make_unique<FPSCameraController>(camera);
		else
			cameraCtrl = std::make_unique<CubeCameraController>(camera);
		activeEP.setCamera(&camera);
	}

	void loadSkybox()
	{
		// TODO this may be allocated with the other textures, but for now let's keep it
		// separate to keep the code cleaner
		measure_ms("Load Skybox", LOGLV_INFO, [this]() {
			app.skybox.image = createSkybox(app);
			if (app.skybox.image.handle == VK_NULL_HANDLE) {
				throw std::runtime_error("Failed to load skybox");
			}
		});
		app.cubeSampler = createTextureCubeSampler(app);
	}

	void recordAllCommandBuffers() { recordMultipassCommandBuffers(app, app.commandBuffers, geometry, netRsrc); }

	void createDescriptorSets()
	{
		// Create the descriptor sets
		auto descriptorSets = createMultipassDescriptorSets(
			app, uniformBuffers, netRsrc.materials, netRsrc.models, app.texSampler, app.cubeSampler);

		//// Store them into app resources
		// A descriptor set for the view-dependant stuff
		app.res.descriptorSets->add("view_res", descriptorSets[0]);
		// One for the shader-dependant stuff
		app.res.descriptorSets->add("gbuffer_res", descriptorSets[1]);
		// One descriptor set per material
		for (unsigned i = 0; i < netRsrc.materials.size(); ++i) {
			auto& mat = netRsrc.materials[i];
			auto descSet = descriptorSets[2 + i];
			mat.descriptorSet = descSet;
			app.res.descriptorSets->add(mat.name, descSet);
		}
		// One descriptor set per object (model)
		for (unsigned i = 0; i < netRsrc.models.size(); ++i) {
			app.res.descriptorSets->add(
				netRsrc.models[i].name, descriptorSets[2 + netRsrc.materials.size() + i]);
		}
	}

	void cleanupSwapChain()
	{
		// Destroy the gbuffer and all its attachments
		app.gBuffer.destroyTransient(app.device);
		// Destroy the swapchain and all its images and framebuffers
		app.swapChain.destroyTransient(app.device);
		vkDestroyPipeline(app.device, app.skybox.pipeline, nullptr);

		vkDestroyRenderPass(app.device, app.renderPass, nullptr);

		vkFreeCommandBuffers(app.device,
			app.commandPool,
			static_cast<uint32_t>(app.commandBuffers.size()),
			app.commandBuffers.data());
	}

	void cleanup()
	{
		cleanupSwapChain();

		unmapBuffersMemory(app.device,
			{
				geometry.vertexBuffer,
				geometry.indexBuffer,
			});
		uniformBuffers.unmapAllBuffers();

		vkDestroySampler(app.device, app.texSampler, nullptr);
		vkDestroySampler(app.device, app.cubeSampler, nullptr);
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
		destroyImage(app.device, app.skybox.image);

		destroyAllBuffers(app.device,
			{ geometry.indexBuffer, geometry.vertexBuffer, app.screenQuadBuffer, app.skybox.buffer });
		uniformBuffers.cleanup();

		vkDestroyPipelineCache(app.device, app.pipelineCache, nullptr);

		app.res.cleanup();
		app.cleanup();
	}

	static void onWindowResized(GLFWwindow* window, int, int)
	{
		auto appl = reinterpret_cast<VulkanClient*>(glfwGetWindowUserPointer(window));
		appl->recreateSwapChain();
	}

	static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
	{
		constexpr double centerX = cfg::WIDTH / 2.0, centerY = cfg::HEIGHT / 2.0;
		static bool firstTime = true;

		if (!firstTime) {
			auto appl = reinterpret_cast<VulkanClient*>(glfwGetWindowUserPointer(window));
			appl->cameraCtrl->turn(xpos - centerX, centerY - ypos);
		}
		firstTime = false;

		// info("xpos = ", xpos, " xoff = ", xpos - center);
		// centerX = xpos;
		// centerY = ypos;
		glfwSetCursorPos(window, centerX, centerY);
	}

	static void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int)
	{
		if (action != GLFW_PRESS)
			return;

		auto appl = reinterpret_cast<VulkanClient*>(glfwGetWindowUserPointer(window));
		switch (key) {
		case GLFW_KEY_Q:
			appl->disconnect();
			glfwSetWindowShouldClose(window, GLFW_TRUE);
			break;
		case GLFW_KEY_G:
			appl->shaderOpts.flip(ShaderOpts::SHOW_GBUF_TEX);
			break;
		case GLFW_KEY_N:
			appl->shaderOpts.flip(ShaderOpts::USE_NORMAL_MAP);
			break;
		case GLFW_KEY_T:
			gLimitFrameTime = !gLimitFrameTime;
			break;
		case GLFW_KEY_KP_ADD:
			appl->cameraCtrl->cameraSpeed += 10;
			break;
		case GLFW_KEY_KP_SUBTRACT:
			appl->cameraCtrl->cameraSpeed -= 10;
			break;
		case GLFW_KEY_F4: {
			// Toggle windowed fullscreen
			const auto mode = glfwGetVideoMode(appl->app.monitor);
			if (appl->fullscreen) {
				glfwSetWindowMonitor(
					window, nullptr, 100, 100, cfg::WIDTH, cfg::HEIGHT, mode->refreshRate);
			} else {
				glfwSetWindowMonitor(
					window, nullptr, 0, 0, mode->width, mode->height, mode->refreshRate);
			}
			appl->fullscreen = !appl->fullscreen;
		} break;
		default:
			break;
		}
	}
};

int main(int argc, char** argv)
{
	if (!Endpoint::initEP()) {
		err("Failed to initialize sockets.");
		return EXIT_FAILURE;
	}
	if (!xplatEnableExitHandler()) {
		err("Failed to enable exit handler!");
		return EXIT_FAILURE;
	}
	xplatSetExitHandler([]() {
		if (Endpoint::cleanupEP())
			info("Successfully cleaned up sockets.");
		else
			err("Failed to cleanup sockets!");
	});

	// Parse args
	int i = argc - 1;
	std::string serverIp = "127.0.0.1";
	int posArgs = 0;
	while (i > 0) {
		if (strlen(argv[i]) < 2) {
			err("Invalid flag: -");
			return EXIT_FAILURE;
		}
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'c':
				gUseCamera = false;
				break;
			case 'v': {
				int lv = 1;
				unsigned j = 2;
				while (j < strlen(argv[i]) && argv[i][j] == 'v') {
					++lv;
					++j;
				}
				gDebugLv = static_cast<LogLevel>(lv);
			} break;
			case 'n':
				gColoredLogs = false;
				break;
			default:
				std::cout << "Usage: " << argv[0] << " [-c (use camera)] [-n (no colored logs)]\n";
				break;
			}
		} else {
			// Positional args: [serverIp]
			switch (posArgs++) {
			case 0:
				serverIp = std::string{ argv[i] };
				break;
			default:
				break;
			}
		}
		--i;
	}

	VulkanClient app;

	try {
		app.run(serverIp.c_str());
	} catch (const std::runtime_error& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
