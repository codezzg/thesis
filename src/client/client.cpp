/** @author Giacomo Parolini, 2018 */
#include "FPSCounter.hpp"
#include "application.hpp"
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
#include "phys_device.hpp"
#include "pipelines.hpp"
#include "profile.hpp"
#include "renderpass.hpp"
#include "shader_opts.hpp"
#include "shared_resources.hpp"
#include "swap.hpp"
#include "textures.hpp"
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

bool gUseCamera = false;
bool gIsDebug = false;
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

		connectToServer(ip);

		if (!gIsDebug)
			measure_ms("Init Vulkan", LOGLV_INFO, [this]() { initVulkan(); });   // deferred rendering
		else
			initVulkanForward();   // forward rendering

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

	ClientPassiveEndpoint passiveEP;
	ClientActiveEndpoint activeEP;
	ClientReliableEndpoint relEP;
	int64_t curFrame = -1;

	std::vector<VkCommandBuffer> swapCommandBuffers;

	/** The semaphores are owned by `app.res`. We save their handles rather than querying them
	 *  each frame for performance reasons.
	 */
	VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;

	/** Struct containing geometry data (vertex/index buffers + metadata) */
	Geometry geometry;

	/** Single buffer containing all uniform buffer objects needed */
	CombinedUniformBuffers uniformBuffers;

	NetworkResources netRsrc;
	VkSampler texSampler = VK_NULL_HANDLE;

	/** Memory area staging vertices and indices coming from the server */
	std::vector<uint8_t> streamingBuffer;
	uint64_t nVertices = 0;
	uint64_t nIndices = 0;

	Camera camera;
	std::unique_ptr<CameraController> cameraCtrl;

	ShaderOpts shaderOpts;

	static constexpr VkDeviceSize VERTEX_BUFFER_SIZE = megabytes(16);
	static constexpr VkDeviceSize INDEX_BUFFER_SIZE = megabytes(16);

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
		swapCommandBuffers = createSwapChainCommandBuffers(app, app.commandPool);
		app.pipelineCache = createPipelineCache(app);

		app.descriptorPool = createDescriptorPool(app, netRsrc);

		// Initialize resource maps
		app.res.init(app.device, app.descriptorPool);

		// Create pipelines
		const auto descSetLayouts = createMultipassDescriptorSetLayouts(app);
		app.res.descriptorSetLayouts->add("view_res", descSetLayouts[0]);
		app.res.descriptorSetLayouts->add("shader_res", descSetLayouts[1]);
		app.res.descriptorSetLayouts->add("mat_res", descSetLayouts[2]);
		app.res.descriptorSetLayouts->add("obj_res", descSetLayouts[3]);

		app.res.pipelineLayouts->add("multi", createPipelineLayout(app, descSetLayouts));

		app.gBuffer.pipeline = createGBufferPipeline(app);
		app.swapChain.pipeline = createSwapChainPipeline(app);

		createDescriptorSetsForMaterials();

		recordAllCommandBuffers();

		createSemaphores();

		prepareCamera();
	}

	void initVulkanForward()
	{
		// Create basic Vulkan resources
		app.swapChain = createSwapChain(app);
		app.swapChain.imageViews = createSwapChainImageViews(app, app.swapChain);
		app.renderPass = createForwardRenderPass(app);

		// app.commandPool = createCommandPool(app);
		app.swapChain.depthImage = createDepthImage(app);
		app.swapChain.framebuffers = createSwapChainFramebuffers(app, app.swapChain);
		swapCommandBuffers = createSwapChainCommandBuffers(app, app.commandPool);
		app.pipelineCache = createPipelineCache(app);

		app.descriptorPool = createDescriptorPool(app, netRsrc);

		// Initialize resource maps
		app.res.init(app.device, app.descriptorPool);

		// Create pipelines
		app.res.descriptorSetLayouts->add("swap", createSwapChainDebugDescriptorSetLayout(app));
		app.res.pipelineLayouts->add(
			"swap", createPipelineLayout(app, { app.res.descriptorSetLayouts->get("swap") }));
		app.swapChain.pipeline = createSwapChainDebugPipeline(app);
		app.res.descriptorSets->add("swap",
			createSwapChainDebugDescriptorSet(
				app, uniformBuffers, netRsrc.defaults.diffuseTex, texSampler));

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

	void connectToServer(const char* serverIp)
	{
		relEP.startActive(serverIp, cfg::RELIABLE_PORT, SOCK_STREAM);
		relEP.runLoop();
		// Wait for handshake to complete
		if (!relEP.await(std::chrono::seconds{ 10 })) {
			throw std::runtime_error("Failed connecting to server!");
		}

		// Retreive one-time data from server
		{
			constexpr std::size_t ONE_TIME_DATA_BUFFER_SIZE = 1 << 25;
			ClientTmpResources resources{ ONE_TIME_DATA_BUFFER_SIZE };
			relEP.resources = &resources;

			// Tell TCP thread to receive the data
			relEP.proceed();
			measure_ms("Recv Assets", LOGLV_INFO, [this]() {
				if (!relEP.await(std::chrono::seconds{ 10 })) {
					throw std::runtime_error("Failed to receive the one-time data!");
				}
			});

			measure_ms("Check Assets", LOGLV_INFO, [this, &resources]() { checkAssets(resources); });

			// Process the received data
			measure_ms("Load Assets", LOGLV_INFO, [this, &resources]() { loadAssets(resources); });

			relEP.resources = nullptr;
			// Drop the memory used for staging the resources as it's not needed anymore.
		}

		startNetwork(serverIp);

		// Tell TCP thread to send READY and wait for server response
		relEP.proceed();
		if (!relEP.await(std::chrono::seconds{ 10 })) {
			throw std::runtime_error("Connected to server, but server didn't send READY!");
		}
		info("Received READY.");

		// Ready to start the main loop
	}

	/** Check we received all the resources needed by all models */
	void checkAssets(const ClientTmpResources& resources)
	{
		// Collect textures ids into a set (so we can use set_difference later)
		std::set<StringId> textureSet;
		for (const auto& pair : resources.textures)
			textureSet.emplace(pair.first);

		for (const auto& pair : resources.models) {
			const auto& model = pair.second;

			std::set<StringId> neededTextureSet;

			// Check materials, and gather needed textures information in the meantime
			for (const auto& mat : model.materials) {
				if (mat == SID_NONE)
					continue;
				auto it = resources.materials.find(mat);
				if (it != resources.materials.end()) {
					const auto& mat = it->second;
					neededTextureSet.emplace(mat.diffuseTex);
					neededTextureSet.emplace(mat.specularTex);
				} else {
					warn("Material ",
						mat,
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

	void loadAssets(const ClientTmpResources& resources)
	{
		constexpr VkDeviceSize STAGING_BUFFER_SIZE = megabytes(128);

		auto stagingBuffer = createStagingBuffer(app, STAGING_BUFFER_SIZE);
		DEFER([&]() {
			unmapBuffersMemory(app.device, { stagingBuffer });
			destroyBuffer(app.device, stagingBuffer);
		});

		// Save models into permanent storage
		std::copy(resources.models.begin(),
			resources.models.end(),
			std::inserter(netRsrc.models, netRsrc.models.begin()));

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
			for (auto& res : texLoadTasks)
				if (!res.get())
					throw std::runtime_error("Failed to load texture image! Latest error: "s +
								 texLoader.getLatestError());
			texLoader.create(app);
			texSampler = createTextureSampler(app);
		}

		// Prepare materials
		{
			shared::Material dfltMat = { SID_NONE, SID_NONE, SID_NONE, SID_NONE };
			netRsrc.defaults.material = createMaterial(dfltMat, netRsrc);
		}
		for (const auto& pair : resources.materials) {
			netRsrc.materials[pair.first] = createMaterial(pair.second, netRsrc);
		}

		prepareBufferMemory(stagingBuffer);
	}

	void mainLoop(const char* serverIp)
	{
		// startNetwork(serverIp);

		FPSCounter fps;
		fps.start();

		updateMVPUniformBuffer();
		updateCompUniformBuffer();

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
		relEP.close();   // FIXME why it hangs sometimes?

		info("waiting device idle");
		VLKCHECK(vkDeviceWaitIdle(app.device));
	}

	void runFrame()
	{
		static size_t pvs = nVertices, pis = nIndices;

		// Receive network data
		receiveData();

		if (nVertices != pvs || nIndices != pis) {
			pvs = nVertices;
			pis = nIndices;
			VLKCHECK(vkDeviceWaitIdle(app.device));
			info("Re-recording command buffers");
			vkFreeCommandBuffers(app.device,
				app.commandPool,
				static_cast<uint32_t>(swapCommandBuffers.size()),
				swapCommandBuffers.data());
			swapCommandBuffers = createSwapChainCommandBuffers(app, app.commandPool);
			recordAllCommandBuffers();
		}

		updateMVPUniformBuffer();
		updateCompUniformBuffer();

		cameraCtrl->processInput(app.window);

		if (gIsDebug)
			drawFrameForward();
		else
			drawFrame();
	}

	void receiveData()
	{
		if (!passiveEP.dataAvailable())
			return;

		auto totBytes = passiveEP.retreive(streamingBuffer.data(), streamingBuffer.size());

		verbose("BYTES READ (", totBytes, ") = ");
		dumpBytes(streamingBuffer.data(), streamingBuffer.size(), 50, LOGLV_VERBOSE);

		// streamingBuffer now contains [chunk0|chunk1|...]

		unsigned bytesProcessed = 0;
		int64_t bytesLeft = totBytes;
		assert(bytesLeft <= static_cast<int64_t>(streamingBuffer.size()));

		while (bytesProcessed < totBytes) {
			debug("Processing chunk at offset ", bytesProcessed);
			const auto bytesInChunk = processChunk(streamingBuffer.data() + bytesProcessed, bytesLeft);
			bytesLeft -= bytesInChunk;
			bytesProcessed += bytesInChunk;
			assert(bytesLeft >= 0);
		}
	}

	/** Receives a pointer to a byte buffer and tries to read an UpdatePacket chunk from it.
	 *  Will not try to read more than `maxBytesToRead` bytes from the buffer.
	 *  An UpdatePacket chunk consists of a ChunkHeader followed by a payload.
	 *  If a chunk is correctly read from the buffer, its content is interpreted and used to
	 *  update the proper vertices or indices of a model.
	 *  @return The number of bytes read, (aka the offset of the next chunk if there are more chunks after this)
	 */
	std::size_t processChunk(uint8_t* ptr, std::size_t maxBytesToRead)
	{
		if (maxBytesToRead <= sizeof(udp::ChunkHeader)) {
			err("Buffer given to processChunk has not enough room for a Header + Payload!");
			return maxBytesToRead;
		}

		//// Read the header
		const auto header = reinterpret_cast<const udp::ChunkHeader*>(ptr);

		std::size_t dataSize = 0;
		void* dataPtr = nullptr;
		switch (header->dataType) {
		case udp::DataType::VERTEX:
			dataSize = sizeof(Vertex);
			dataPtr = geometry.vertexBuffer.ptr;
			break;
		case udp::DataType::INDEX:
			dataSize = sizeof(Index);
			dataPtr = geometry.indexBuffer.ptr;
			break;
		default: {
			std::stringstream ss;
			ss << "Invalid data type " << int(header->dataType) << " in Update Chunk!";
			throw std::runtime_error(ss.str());
		} break;
		}

		assert(dataSize != 0 && dataPtr != nullptr);

		const auto chunkSize = sizeof(udp::ChunkHeader) + dataSize * header->len;

		if (chunkSize > maxBytesToRead) {
			err("processChunk would read past the allowed memory area!");
			return maxBytesToRead;
		}

		auto it = geometry.locations.find(header->modelId);
		if (it == geometry.locations.end()) {
			warn("Received an Update Chunk for inexistent model ", header->modelId, "!");
			// XXX
			return chunkSize;
		}

		//// Update the model

		debug("Updating model ",
			header->modelId,
			" / (type = ",
			int(header->dataType),
			") from ",
			header->start,
			" to ",
			header->start + header->len);
		auto& loc = it->second;
		// Use the correct offset into the vertex/index buffer
		const auto offset = header->dataType == udp::DataType::VERTEX ? loc.vertexOff : loc.indexOff;
		dataPtr = reinterpret_cast<uint8_t*>(dataPtr) + offset;

		memcpy(dataPtr, ptr + sizeof(header), dataSize * header->len);

		return chunkSize;
	};

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
		if (gIsDebug) {
			app.renderPass = createForwardRenderPass(app);
			app.swapChain.pipeline = createSwapChainDebugPipeline(app);
			app.swapChain.framebuffers = createSwapChainFramebuffers(app, app.swapChain);
		} else {
			app.gBuffer.createAttachments(app);
			app.renderPass = createMultipassRenderPass(app);

			updateGBufferDescriptors(app, app.res.descriptorSets->get("shader_res"), texSampler);

			app.gBuffer.pipeline = createGBufferPipeline(app);
			app.swapChain.pipeline = createSwapChainPipeline(app);
			app.swapChain.framebuffers = createSwapChainMultipassFramebuffers(app, app.swapChain);
		}
		swapCommandBuffers = createSwapChainCommandBuffers(app, app.commandPool);

		recordAllCommandBuffers();
		updateMVPUniformBuffer();
		updateCompUniformBuffer();
	}

	void createSemaphores()
	{
		imageAvailableSemaphore = app.res.semaphores->create("image_available");
		renderFinishedSemaphore = app.res.semaphores->create("render_finished");
	}

	void drawFrameForward()
	{
		uint32_t imageIndex;
		if (!acquireNextSwapImage(app, imageAvailableSemaphore, imageIndex)) {
			recreateSwapChain();
			return;
		}

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &swapCommandBuffers[imageIndex];

		VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(app.queues.graphics, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
			throw std::runtime_error("failed to submit draw command buffer!");
		}

		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapChains[] = { app.swapChain.handle };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;

		presentInfo.pImageIndices = &imageIndex;

		auto result = vkQueuePresentKHR(app.queues.present, &presentInfo);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			recreateSwapChain();
		} else if (result != VK_SUCCESS) {
			throw std::runtime_error("failed to present swap chain image!");
		}

		VLKCHECK(vkQueueWaitIdle(app.queues.present));
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
		submitInfo.pCommandBuffers = &swapCommandBuffers[imageIndex];

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

	void updateMVPUniformBuffer()
	{
		static auto startTime = std::chrono::high_resolution_clock::now();

		auto ubo = uniformBuffers.getMVP();

		if (gUseCamera) {
			ubo->model = glm::mat4{ 1.0f };
		} else {
			auto currentTime = std::chrono::high_resolution_clock::now();
			float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime)
					     .count();
			ubo->model =
				glm::rotate(glm::mat4{ 1.0f }, time * glm::radians(89.f), glm::vec3{ 0.f, -1.f, 0.f });
			// ubo->view = glm::lookAt(glm::vec3{ 14, 14, 14 },
			// glm::vec3{ 0, 0, 0 }, glm::vec3{ 0, 1, 0 });
		}
		ubo->view = camera.viewMatrix();
		// ubo->proj = camera.projMatrix();
		ubo->proj = glm::perspective(glm::radians(60.f),
			app.swapChain.extent.width / float(app.swapChain.extent.height),
			0.1f,
			300.f);
		// Flip y
		ubo->proj[1][1] *= -1;
	}

	void updateCompUniformBuffer()
	{
		auto ubo = uniformBuffers.getComp();
		ubo->viewPos = glm::vec4{ camera.position.x, camera.position.y, camera.position.z, 0.f };
		ubo->opts = shaderOpts.getRepr();
		verbose("viewPos = ", glm::to_string(ubo->viewPos));
	}

	void prepareBufferMemory(Buffer& stagingBuffer)
	{
		// Find out the optimal offsets for uniform buffers, accounting for minimum align
		VkDeviceSize uboSize = 0;
		const auto uboAlign = findMinUboAlign(app.physicalDevice);
		// FIXME: this approach is only feasible for at most 2 UBOs, as it grows combinatorily
		if (sizeof(MVPUniformBufferObject) <= uboAlign) {
			uniformBuffers.offsets.mvp = 0;
			uboSize += sizeof(MVPUniformBufferObject);
			const auto padding = uboAlign - uboSize;
			uboSize += padding;
			uniformBuffers.offsets.comp = uboSize;
			uboSize += sizeof(CompositionUniformBufferObject);
		} else {
			uniformBuffers.offsets.comp = 0;
			uboSize += sizeof(CompositionUniformBufferObject);
			const auto padding = uboAlign - uboSize;
			uboSize += padding;
			uniformBuffers.offsets.mvp = uboSize;
			uboSize += sizeof(MVPUniformBufferObject);
		}

		// Create vertex, index and uniform buffers. These buffers are all created una-tantum.
		BufferAllocator bufAllocator;

		geometry.locations = addVertexAndIndexBuffers(
			bufAllocator, geometry.vertexBuffer, geometry.indexBuffer, netRsrc.models);

		// uniform buffers
		bufAllocator.addBuffer(uniformBuffers,
			uboSize,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		// screen quad buffer
		bufAllocator.addBuffer(app.screenQuadBuffer, getScreenQuadBufferProperties());

		bufAllocator.create(app);

		// Map device memory to host
		mapBuffersMemory(app.device,
			{
				&geometry.vertexBuffer,
				&geometry.indexBuffer,
				&uniformBuffers,
			});

		// Allocate enough memory to contain all vertices and indices
		streamingBuffer.resize(megabytes(16));
		// std::accumulate(
		// netRsrc.models.begin(), netRsrc.models.end(), 0, [](auto acc, const auto& pair) {
		// return acc + pair.second.nVertices + pair.second.nIndices;
		//}));

		fillScreenQuadBuffer(app, app.screenQuadBuffer, stagingBuffer);
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

	void recordAllCommandBuffers()
	{
		if (gIsDebug) {
			recordSwapChainDebugCommandBuffers(app, swapCommandBuffers, nIndices, geometry);
		} else {
			recordMultipassCommandBuffers(app, swapCommandBuffers, nIndices, geometry, netRsrc);
		}
	}

	void createDescriptorSetsForMaterials()
	{
		// Gather materials and names into vectors
		std::vector<Material> materials;
		std::vector<StringId> materialNames;

		if (netRsrc.materials.size() > 0) {
			materials.reserve(netRsrc.materials.size());
			materialNames.reserve(netRsrc.materials.size());
			for (const auto& pair : netRsrc.materials) {
				materialNames.emplace_back(pair.first);
				materials.emplace_back(pair.second);
			}
		} else {
			materials.emplace_back(netRsrc.defaults.material);
			materialNames.emplace_back(SID_NONE);
		}

		// Create the descriptor sets
		auto descriptorSets = createMultipassDescriptorSets(app, uniformBuffers, materials, texSampler);

		// Store them into app resources
		app.res.descriptorSets->add("view_res", descriptorSets[0]);
		app.res.descriptorSets->add("shader_res", descriptorSets[1]);
		for (unsigned i = 0; i < materials.size(); ++i) {
			auto descSet = descriptorSets[2 + i];
			netRsrc.materials[materialNames[i]].descriptorSet = descSet;
			app.res.descriptorSets->add(materialNames[i], descSet);
		}
		// TODO multiple objects (models)
		app.res.descriptorSets->add("obj_res", descriptorSets[descriptorSets.size() - 1]);
	}

	void cleanupSwapChain()
	{
		// Destroy the gbuffer and all its attachments
		if (!gIsDebug)
			app.gBuffer.destroyTransient(app.device);
		// Destroy the swapchain and all its images and framebuffers
		app.swapChain.destroyTransient(app.device);

		vkDestroyRenderPass(app.device, app.renderPass, nullptr);

		vkFreeCommandBuffers(app.device,
			app.commandPool,
			static_cast<uint32_t>(swapCommandBuffers.size()),
			swapCommandBuffers.data());
	}

	void cleanup()
	{
		cleanupSwapChain();

		unmapBuffersMemory(app.device,
			{
				geometry.vertexBuffer,
				geometry.indexBuffer,
				uniformBuffers,
			});

		vkDestroySampler(app.device, texSampler, nullptr);
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

		destroyAllBuffers(app.device,
			{ uniformBuffers, geometry.indexBuffer, geometry.vertexBuffer, app.screenQuadBuffer });

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
		static constexpr double centerX = cfg::WIDTH / 2.0, centerY = cfg::HEIGHT / 2.0;
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
		default:
			break;
		}
	}
};

int main(int argc, char** argv)
{
	if (!Endpoint::init()) {
		err("Failed to initialize sockets.");
		return EXIT_FAILURE;
	}
	if (!xplatEnableExitHandler()) {
		err("Failed to enable exit handler!");
		return EXIT_FAILURE;
	}
	xplatSetExitHandler([]() {
		if (Endpoint::cleanup())
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
			case 'd':
				gIsDebug = true;
				break;
			case 'c':
				gUseCamera = true;
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
				std::cout
					<< "Usage: " << argv[0]
					<< " [-c (use camera)] [-d (debug mode, aka use forward rendering)] [-n (no colored logs)]\n";
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
