/** @author Giacomo Parolini, 2018 */
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
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
#include "images.hpp"
#include "logging.hpp"
#include "multipass.hpp"
#include "phys_device.hpp"
#include "pipelines.hpp"
#include "renderpass.hpp"
#include "shared_resources.hpp"
#include "swap.hpp"
#include "textures.hpp"
#include "units.hpp"
#include "validation.hpp"
#include "vertex.hpp"
#include "vulk_errors.hpp"
#include "vulk_utils.hpp"
#include "window.hpp"
#include "xplatform.hpp"
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
	void run(const char* ip) {
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
			initVulkan();   // deferred rendering
		else
			initVulkanForward();   // forward rendering

		mainLoop(ip);
		cleanup();
	}

private:
	Application app;

	ClientPassiveEndpoint passiveEP;
	ClientActiveEndpoint activeEP;
	ClientReliableEndpoint relEP;
	int64_t curFrame = -1;

	Camera camera;
	std::unique_ptr<CameraController> cameraCtrl;

	std::vector<VkCommandBuffer> swapCommandBuffers;

	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;

	Buffer vertexBuffer;
	Buffer indexBuffer;
	// Single buffer to contain all uniform buffer objects needed
	CombinedUniformBuffers uniformBuffers;

	NetworkResources netRsrc;
	VkSampler texSampler;

	/** Pointer to the memory area staging vertices and indices coming from the server */
	uint8_t* streamingBufferData = nullptr;
	uint64_t nVertices = 0;
	uint64_t nIndices = 0;

	bool showGBufTex = false;

	static constexpr VkDeviceSize VERTEX_BUFFER_SIZE = megabytes(16);
	static constexpr VkDeviceSize INDEX_BUFFER_SIZE = megabytes(16);

	void initVulkan() {
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

		// Initialize resource maps
		app.res.init(app.device, app.descriptorPool);

		app.descriptorPool = createDescriptorPool(app);

		// Create pipelines
		app.res.descriptorSetLayouts->add("multi", createMultipassDescriptorSetLayout(app));
		app.res.pipelineLayouts->add(
		        "multi", createPipelineLayout(app, app.res.descriptorSetLayouts->get("multi")));
		app.gBuffer.pipeline = createGBufferPipeline(app);
		app.swapChain.pipeline = createSwapChainPipeline(app);
		app.res.descriptorSets->add("multi",
		        createMultipassDescriptorSet(app,
		                uniformBuffers,
		                netRsrc.defaults.diffuseTex,
		                netRsrc.defaults.specularTex,
		                texSampler));

		recordAllCommandBuffers();

		createSemaphores();

		prepareCamera();
	}

	void initVulkanForward() {
		// Create basic Vulkan resources
		app.swapChain = createSwapChain(app);
		app.swapChain.imageViews = createSwapChainImageViews(app, app.swapChain);
		app.renderPass = createForwardRenderPass(app);

		// app.commandPool = createCommandPool(app);
		app.swapChain.depthImage = createDepthImage(app);
		app.swapChain.framebuffers = createSwapChainFramebuffers(app, app.swapChain);
		swapCommandBuffers = createSwapChainCommandBuffers(app, app.commandPool);
		app.pipelineCache = createPipelineCache(app);

		// Initialize resource maps
		app.res.init(app.device, app.descriptorPool);

		app.descriptorPool = createDescriptorPool(app);

		// Create pipelines
		app.res.descriptorSetLayouts->add("swap", createSwapChainDebugDescriptorSetLayout(app));
		app.res.pipelineLayouts->add(
		        "swap", createPipelineLayout(app, app.res.descriptorSetLayouts->get("swap")));
		app.swapChain.pipeline = createSwapChainDebugPipeline(app);
		app.res.descriptorSets->add("swap",
		        createSwapChainDebugDescriptorSet(
		                app, uniformBuffers, netRsrc.defaults.diffuseTex, texSampler));

		recordAllCommandBuffers();

		createSemaphores();

		prepareCamera();
	}

	void startNetwork(const char* serverIp) {
		passiveEP.startPassive("0.0.0.0", cfg::SERVER_TO_CLIENT_PORT, SOCK_DGRAM);
		passiveEP.runLoop();

		activeEP.startActive(serverIp, cfg::CLIENT_TO_SERVER_PORT, SOCK_DGRAM);
		activeEP.targetFrameTime = SERVER_UPDATE_TIME;
		activeEP.runLoop();
	}

	void connectToServer(const char* serverIp) {
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
			if (!relEP.await(std::chrono::seconds{ 10 })) {
				throw std::runtime_error("Failed to receive the one-time data!");
			}

			checkAssets(resources);

			// Process the received data
			loadAssets(resources);

			relEP.resources = nullptr;
			// Drop the memory used for staging the resources as it's not needed anymore.
		}

		// Tell TCP thread to send READY and wait for server response
		relEP.proceed();
		if (!relEP.await(std::chrono::seconds{ 10 })) {
			throw std::runtime_error("Connected to server, but server didn't send READY!");
		}
		info("Received READY.");

		// Ready to start the main loop
	}

	/** Check we received all the resources needed by all models */
	void checkAssets(const ClientTmpResources& resources) {

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

	void loadAssets(const ClientTmpResources& resources) {
		constexpr VkDeviceSize STAGING_BUFFER_SIZE = megabytes(128);

		auto stagingBuffer = createStagingBuffer(app, STAGING_BUFFER_SIZE);
		DEFER([&]() {
			unmapBuffersMemory(app.device, { stagingBuffer });
			destroyBuffer(app.device, stagingBuffer);
		});

		// Load textures (TODO: use correct materials)
		auto tex = const_cast<std::unordered_map<StringId, shared::Texture>&>(resources.textures);

		TextureLoader texLoader{ stagingBuffer };
		// Create default textures
		texLoader.addTexture(netRsrc.defaults.diffuseTex, "textures/default.jpg", shared::TextureFormat::RGBA);
		texLoader.addTexture(
		        netRsrc.defaults.specularTex, "textures/default_spec.jpg", shared::TextureFormat::GREY);
		if (tex.size() == 0) {
			warn("Received no textures: using default ones.");
		} else {
			for (const auto& pair : resources.textures) {
				if (pair.first == SID_NONE)
					continue;
				texLoader.addTexture(netRsrc.textures[pair.first], pair.second);
			}
		}
		texLoader.create(app);
		texSampler = createTextureSampler(app);

		// Prepare materials
		for (const auto& pair : resources.materials) {
			netRsrc.materials[pair.first] = createMaterial(pair.second, netRsrc);
		}

		prepareBufferMemory(stagingBuffer);
	}

	void mainLoop(const char* serverIp) {
		startNetwork(serverIp);

		FPSCounter fps;
		fps.start();

		updateMVPUniformBuffer();
		updateCompUniformBuffer();

		auto beginTime = std::chrono::high_resolution_clock::now();

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

	void runFrame() {
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

	void receiveData() {
		debug("receive data. curFrame = ", curFrame, ", passive.frame = ", passiveEP.getFrameId());

		if (curFrame >= 0 && passiveEP.getFrameId() == curFrame) {
			debug("Rejecting old frame data");
			return;
		}

		if (!passiveEP.dataAvailable()) {
			debug("data unavailable");
			return;
		}

		// Update frame Id
		curFrame = passiveEP.getFrameId();

		// Copy received data into the streaming buffer
		PayloadHeader phead;
		passiveEP.retreive(
		        phead, reinterpret_cast<Vertex*>(vertexBuffer.ptr), reinterpret_cast<Index*>(indexBuffer.ptr));

		// streamingBufferData now contains [vertices|indices]
		nVertices = phead.nVertices;
		nIndices = phead.nIndices;
		debug("\nn vertices: ", nVertices, ", n indices: ", nIndices);

		// constexpr auto HEADER_SIZE = 2 * sizeof(uint64_t);
		// memcpy(streamingBufferData, data + HEADER_SIZE, nVertices * sizeof(Vertex));
		// memcpy(streamingBufferData + VERTEX_BUFFER_SIZE, data + HEADER_SIZE + nVertices * sizeof(Vertex),
		// nIndices * sizeof(Index));

		// if (curFrame == 1) {
		// std::ofstream of("sb.data", std::ios::binary);
		// for (int i = 0; i < nVertices * sizeof(Vertex) + nIndices * sizeof(Index); ++i)
		// of << ((uint8_t*)streamingBufferData)[i];
		//}

		// std::cerr << "vertex[0] = " << *((Vertex*)streamingBufferData) << std::endl;
		// std::cerr << "vertex[100] = " << *(((Vertex*)streamingBufferData)+100) << std::endl;
		// std::cerr << "index[0] = " << (Index)*(streamingBufferData + nVertices * sizeof(Vertex)) <<
		// std::endl;
	}

	void calcTimeStats(FPSCounter& fps, std::chrono::time_point<std::chrono::high_resolution_clock>& beginTime) {
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

	void recreateSwapChain() {
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

			VLKCHECK(vkFreeDescriptorSets(
			        app.device, app.descriptorPool, 1, &app.res.descriptorSets->get("multi")));
			app.res.descriptorSets->add("multi",
			        createMultipassDescriptorSet(app,
			                uniformBuffers,
			                netRsrc.defaults.diffuseTex,
			                netRsrc.defaults.specularTex,
			                texSampler));

			app.gBuffer.pipeline = createGBufferPipeline(app);
			app.swapChain.pipeline = createSwapChainPipeline(app);
			app.swapChain.framebuffers = createSwapChainMultipassFramebuffers(app, app.swapChain);
		}
		swapCommandBuffers = createSwapChainCommandBuffers(app, app.commandPool);

		recordAllCommandBuffers();
		updateMVPUniformBuffer();
		updateCompUniformBuffer();
	}

	void createSemaphores() {
		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VLKCHECK(vkCreateSemaphore(app.device, &semaphoreInfo, nullptr, &imageAvailableSemaphore));
		VLKCHECK(vkCreateSemaphore(app.device, &semaphoreInfo, nullptr, &renderFinishedSemaphore));
	}

	void drawFrameForward() {
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

	void drawFrame() {
		uint32_t imageIndex;
		if (!acquireNextSwapImage(app, imageAvailableSemaphore, imageIndex)) {
			info("Recreating swap chain");
			recreateSwapChain();
			return;
		}

		renderFrame(imageIndex);
		submitFrame(imageIndex);
	}

	void renderFrame(uint32_t imageIndex) {
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

	void submitFrame(uint32_t imageIndex) {
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

	void updateMVPUniformBuffer() {
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

	void updateCompUniformBuffer() {
		auto ubo = uniformBuffers.getComp();
		ubo->viewPos = glm::vec4{
			camera.position.x,
			camera.position.y,
			camera.position.z,
			showGBufTex,
		};
		verbose("viewPos = ", ubo->viewPos);
	}

	void prepareBufferMemory(Buffer& stagingBuffer) {
		streamingBufferData = new uint8_t[VERTEX_BUFFER_SIZE + INDEX_BUFFER_SIZE];

		// Find out the proper offsets for uniform buffers
		VkDeviceSize uboSize = 0;
		const auto uboAlign = findMinUboAlign(app.physicalDevice);
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

		// These buffers are all created una-tantum.
		BufferAllocator bufAllocator;

		// vertex buffer
		bufAllocator.addBuffer(vertexBuffer,
		        VERTEX_BUFFER_SIZE,
		        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		// index buffer
		bufAllocator.addBuffer(indexBuffer,
		        INDEX_BUFFER_SIZE,
		        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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
		                &vertexBuffer,
		                &indexBuffer,
		                &uniformBuffers,
		        });

		fillScreenQuadBuffer(app, app.screenQuadBuffer, stagingBuffer);
	}

	void prepareCamera() {
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

	void recordAllCommandBuffers() {
		if (gIsDebug) {
			recordSwapChainDebugCommandBuffers(
			        app, swapCommandBuffers, nIndices, vertexBuffer, indexBuffer);
		} else {
			recordMultipassCommandBuffers(app, swapCommandBuffers, nIndices, vertexBuffer, indexBuffer);
		}
	}

	void cleanupSwapChain() {
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

	void cleanup() {
		cleanupSwapChain();

		unmapBuffersMemory(app.device,
		        {
		                vertexBuffer,
		                indexBuffer,
		                uniformBuffers,
		        });

		vkDestroySampler(app.device, texSampler, nullptr);
		{
			std::vector<Image> imagesToDestroy;
			imagesToDestroy.reserve(2 + netRsrc.textures.size());
			imagesToDestroy.emplace_back(netRsrc.defaults.diffuseTex);
			imagesToDestroy.emplace_back(netRsrc.defaults.specularTex);
			for (const auto& tex : netRsrc.textures)
				imagesToDestroy.emplace_back(tex.second);
			destroyAllImages(app.device, imagesToDestroy);
		}

		destroyAllBuffers(app.device, { uniformBuffers, indexBuffer, vertexBuffer, app.screenQuadBuffer });

		vkDestroyPipelineCache(app.device, app.pipelineCache, nullptr);

		vkDestroySemaphore(app.device, renderFinishedSemaphore, nullptr);
		vkDestroySemaphore(app.device, imageAvailableSemaphore, nullptr);

		delete[] streamingBufferData;

		app.res.cleanup();
		app.cleanup();
	}

	static void onWindowResized(GLFWwindow* window, int, int) {
		auto appl = reinterpret_cast<VulkanClient*>(glfwGetWindowUserPointer(window));
		appl->recreateSwapChain();
	}

	static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
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

	static void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int) {
		if (action != GLFW_PRESS)
			return;

		auto appl = reinterpret_cast<VulkanClient*>(glfwGetWindowUserPointer(window));
		switch (key) {
		case GLFW_KEY_Q:
			glfwSetWindowShouldClose(window, GLFW_TRUE);
			break;
		case GLFW_KEY_G:
			appl->showGBufTex = !appl->showGBufTex;
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

int main(int argc, char** argv) {
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
			default:
				std::cout << "Usage: " << argv[0]
				          << " [-c (use camera)] [-d (debug mode, aka use forward rendering)]\n";
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
