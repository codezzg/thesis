#include <array>
#include <unordered_map>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <set>
#include <limits>
#include <memory>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <utility>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <chrono>
#include "FPSCounter.hpp"
#include "vertex.hpp"
#include "validation.hpp"
#include "vulk_utils.hpp"
#include "config.hpp"
#include "data.hpp"
#include "window.hpp"
#include "phys_device.hpp"
#include "commands.hpp"
#include "application.hpp"
#include "client_endpoint.hpp"
#include "camera.hpp"
#include "camera_ctrl.hpp"
#include "clock.hpp"
#include "buffers.hpp"
#include "swap.hpp"
#include "formats.hpp"
#include "vulk_errors.hpp"
#include "images.hpp"
#include "renderpass.hpp"
#include "gbuffer.hpp"
#include "textures.hpp"
#include "xplatform.hpp"

// Fuck off, Windows
#undef max
#undef min

using namespace std::literals::string_literals;
using std::size_t;

// XXX: DEBUG
bool useCamera = false;
bool isDebug = false;

class HelloTriangleApplication final {
public:
	void run() {
		app.init();

		glfwSetWindowUserPointer(app.window, this);
		glfwSetWindowSizeCallback(app.window, onWindowResized);
		if (useCamera) {
			glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			glfwSetCursorPosCallback(app.window, cursorPosCallback);
		}

		initVulkan();
		connectToServer();
		mainLoop();
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
	VkCommandBuffer gbufCommandBuffer;

	VkSemaphore gBufRenderFinishedSemaphore;
	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;

	Buffer vertexBuffer;
	Buffer indexBuffer;
	Buffer mvpUniformBuffer;
	Buffer compUniformBuffer;

	Image texDiffuseImage;
	Image texSpecularImage;

	/** Pointer to the memory area staging vertices and indices coming from the server */
	uint8_t *streamingBufferData = nullptr;
	uint64_t nVertices = 0;
	uint64_t nIndices = 0;
	/** Permanently mapped pointer to device vertex data (vertexBuffer.memory) */
	void *vertexData = nullptr;
	/** Permanently mapped pointer to device index data (indexBuffer.memory) */
	void *indexData = nullptr;
	static constexpr size_t VERTEX_BUFFER_SIZE = 1<<24;
	static constexpr size_t INDEX_BUFFER_SIZE = 1<<24;


	void initVulkan() {
		app.swapChain = createSwapChain(app);

		{
			// Create the gbuffer for the geometry pass
			app.gBuffer.createAttachments(app);
			app.gBuffer.renderPass = createGeometryRenderPass(app);
			app.gBuffer.framebuffer = createGBufferFramebuffer(app);
			app.gBuffer.descriptorSetLayout = createGBufferDescriptorSetLayout(app);
			app.gBuffer.pipelineLayout = createGBufferPipelineLayout(app);
			app.gBuffer.pipeline = createGBufferPipeline(app);
		}

		app.commandPool = createCommandPool(app);
		app.screenQuadBuffer = createScreenQuadVertexBuffer(app);

		{
			// Setup the deferred lighting pass and the swapchain
			app.swapChain.depthImage = createDepthImage(app);
			app.swapChain.imageViews = createSwapChainImageViews(app);
			const auto lightRenderPass = createLightingRenderPass(app);
			app.swapChain.renderPass = lightRenderPass;
			// Create a framebuffer for each image in the swap chain for the presentation
			app.swapChain.framebuffers = createSwapChainFramebuffers(app);
			if (isDebug)
				app.swapChain.descriptorSetLayout = createSwapChainDebugDescriptorSetLayout(app);
			else
				app.swapChain.descriptorSetLayout = createSwapChainDescriptorSetLayout(app);
			app.swapChain.pipelineLayout = createSwapChainPipelineLayout(app);
			app.swapChain.pipeline = createSwapChainPipeline(app, isDebug ? "3d" : "composition");
		}

		{
			// Load textures
			texDiffuseImage = createTextureImage(app, cfg::TEXTURE_PATH, TextureFormat::RGBA);
			texDiffuseImage.sampler = createTextureSampler(app);
			texSpecularImage = createTextureImage(app, cfg::TEXTURE_PATH, TextureFormat::GREY);
			texSpecularImage.sampler = createTextureSampler(app);
		}

		prepareBufferMemory();

		{
			// Create descriptor sets and command buffers for G-Buffer
			app.gBuffer.descriptorPool = createGBufferDescriptorPool(app);
			app.gBuffer.descriptorSet = createGBufferDescriptorSet(app, app.gBuffer.descriptorSetLayout,
					mvpUniformBuffer, texDiffuseImage, texSpecularImage);
			gbufCommandBuffer = createGBufferCommandBuffer(app, nIndices, vertexBuffer,
					indexBuffer, mvpUniformBuffer, app.gBuffer.descriptorSet);
		}

		{
			// Create descriptor sets and command buffers for lighting pass
			app.swapChain.descriptorPool = createSwapChainDescriptorPool(app);
			if (isDebug) {
				app.swapChain.descriptorSet = createSwapChainDebugDescriptorSet(app,
							app.swapChain.descriptorSetLayout,
							mvpUniformBuffer, texDiffuseImage);
				swapCommandBuffers = createSwapChainDebugCommandBuffers(app, nIndices,
					vertexBuffer, indexBuffer,
					mvpUniformBuffer, app.swapChain.descriptorSet);
			} else {
				app.swapChain.descriptorSet = createSwapChainDescriptorSet(app,
							app.swapChain.descriptorSetLayout,
							compUniformBuffer, texDiffuseImage);
				swapCommandBuffers = createSwapChainCommandBuffers(app, nIndices,
					compUniformBuffer, app.swapChain.descriptorSet);
			}
		}

		createSemaphores();

		prepareCamera();
	}

	void connectToServer() {
		relEP.startActive(cfg::SERVER_RELIABLE_IP, cfg::SERVER_RELIABLE_PORT, SOCK_STREAM);
		relEP.runLoop();
		if (!relEP.await(std::chrono::seconds{ 10 })) {
			throw std::runtime_error("Failed connecting to server!");
		}

		// Tell TCP thread to send READY msg
		relEP.proceed();
		if (!relEP.await(std::chrono::seconds{ 10 })) {
			throw std::runtime_error("Connected to server, but server didn't send READY!");
		}
		std::cerr << "Received READY.\n";

		// Ready to start the main loop
	}

	void mainLoop() {
		startNetwork();

		FPSCounter fps;
		fps.start();

		//updateVertexBuffer();
		//updateIndexBuffer();
		updateMVPUniformBuffer();
		updateCompUniformBuffer();

		auto& clock = Clock::instance();
		auto beginTime = std::chrono::high_resolution_clock::now();

		while (!glfwWindowShouldClose(app.window)) {
			glfwPollEvents();

			runFrame();

			calcTimeStats(fps, beginTime);
		}

		passiveEP.close();
		activeEP.close();
		relEP.close();
		vkDeviceWaitIdle(app.device);
	}

	void startNetwork() {
		passiveEP.startPassive(cfg::CLIENT_PASSIVE_IP, cfg::CLIENT_PASSIVE_PORT, SOCK_DGRAM);
		passiveEP.runLoop();

		activeEP.startActive(cfg::CLIENT_ACTIVE_IP, cfg::CLIENT_ACTIVE_PORT, SOCK_DGRAM);
		activeEP.runLoop();
	}

	void runFrame() {
		static size_t pvs = nVertices,
		              pis = nIndices;

		// Receive network data
		receiveData();

		if (nVertices != pvs || nIndices != pis) {
			pvs = nVertices;
			pis = nIndices;
			vkDeviceWaitIdle(app.device);
			vkFreeCommandBuffers(app.device, app.commandPool,
				static_cast<uint32_t>(swapCommandBuffers.size()),
				swapCommandBuffers.data());
			if (isDebug) {
				swapCommandBuffers = createSwapChainDebugCommandBuffers(app, nIndices,
						vertexBuffer, indexBuffer,
						mvpUniformBuffer, app.swapChain.descriptorSet);
			} else {
				swapCommandBuffers = createSwapChainCommandBuffers(app, nIndices,
					compUniformBuffer, app.swapChain.descriptorSet);
			}
		}

		//updateVertexBuffer();
		//updateIndexBuffer();
		updateMVPUniformBuffer();
		updateCompUniformBuffer();

		cameraCtrl->processInput(app.window);

		if (isDebug)
			drawFrame2();
		else
			drawFrame();
	}

	void receiveData() {
		//std::cerr << "receive data. curFrame = " << curFrame << ", passive.get = " << passiveEP.getFrameId() << "\n";
		if (curFrame >= 0 && passiveEP.getFrameId() == curFrame)
			return;

		if (!passiveEP.dataAvailable())
			return;

		// Update frame Id
		curFrame = passiveEP.getFrameId();

		// Copy received data into the streaming buffer
		PayloadHeader phead;
		passiveEP.retreive(phead, reinterpret_cast<Vertex*>(vertexData), reinterpret_cast<Index*>(indexData));

		// streamingBufferData now contains [vertices|indices]
		nVertices = phead.nVertices;
		nIndices = phead.nIndices;
		std::cerr << "\nn vertices: " << nVertices << ", n indices: " << nIndices << "\n";

		//constexpr auto HEADER_SIZE = 2 * sizeof(uint64_t);
		//memcpy(streamingBufferData, data + HEADER_SIZE, nVertices * sizeof(Vertex));
		//memcpy(streamingBufferData + VERTEX_BUFFER_SIZE, data + HEADER_SIZE + nVertices * sizeof(Vertex),
				//nIndices * sizeof(Index));

		//if (curFrame == 1) {
			//std::ofstream of("sb.data", std::ios::binary);
			//for (int i = 0; i < nVertices * sizeof(Vertex) + nIndices * sizeof(Index); ++i)
				//of << ((uint8_t*)streamingBufferData)[i];
		//}

		//std::cerr << "vertex[0] = " << *((Vertex*)streamingBufferData) << std::endl;
		//std::cerr << "vertex[100] = " << *(((Vertex*)streamingBufferData)+100) << std::endl;
		//std::cerr << "index[0] = " << (Index)*(streamingBufferData + nVertices * sizeof(Vertex)) << std::endl;
	}

	void calcTimeStats(FPSCounter& fps, std::chrono::time_point<std::chrono::high_resolution_clock>& beginTime) {
		auto& clock = Clock::instance();
		const auto endTime = std::chrono::high_resolution_clock::now();
		float dt = std::chrono::duration_cast<std::chrono::microseconds>(
				endTime - beginTime).count() / 1'000'000.f;
		if (dt > 1.f)
			dt = clock.targetDeltaTime;
		clock.update(dt);
		beginTime = endTime;
		//std::cerr << "dt = " << clock.deltaTime() << " (estimate FPS = " <<
			//1 / clock.deltaTime() << ")\n";

		fps.addFrame();
		fps.report();
	}

	void cleanupSwapChain() {
		// this doesn't destroy the descriptorSetLayout/Pool
		app.gBuffer.destroyTransient(app.device);
		app.swapChain.destroyTransient(app.device);

		vkFreeCommandBuffers(app.device, app.commandPool,
				static_cast<uint32_t>(swapCommandBuffers.size()),
				swapCommandBuffers.data());
		vkFreeCommandBuffers(app.device, app.commandPool, 1, &gbufCommandBuffer);
	}

	void cleanup() {
		cleanupSwapChain();

		vkUnmapMemory(app.device, vertexBuffer.memory);
		vkUnmapMemory(app.device, indexBuffer.memory);

		texDiffuseImage.destroy(app.device);
		texSpecularImage.destroy(app.device);

		app.gBuffer.destroyPersistent(app.device);
		app.swapChain.destroyPersistent(app.device);

		mvpUniformBuffer.destroy(app.device);
		compUniformBuffer.destroy(app.device);
		indexBuffer.destroy(app.device);
		vertexBuffer.destroy(app.device);

		vkDestroySemaphore(app.device, renderFinishedSemaphore, nullptr);
		vkDestroySemaphore(app.device, imageAvailableSemaphore, nullptr);
		vkDestroySemaphore(app.device, gBufRenderFinishedSemaphore, nullptr);
		vkDestroyCommandPool(app.device, app.commandPool, nullptr);

		delete [] streamingBufferData;

		app.cleanup();
	}

	static void onWindowResized(GLFWwindow *window, int, int) {
		auto appl = reinterpret_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
		appl->recreateSwapChain();
	}

	static void cursorPosCallback(GLFWwindow *window, double xpos, double ypos) {
		static double prevX = cfg::WIDTH / 2.0,
		              prevY = cfg::HEIGHT / 2.0;
		auto appl = reinterpret_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
		appl->cameraCtrl->turn(xpos - prevX, prevY - ypos);
		//prevX = xpos;
		//prevY = ypos;
		glfwSetCursorPos(window, prevX, prevY);
	}

	void recreateSwapChain() {
		int width, height;
		glfwGetWindowSize(app.window, &width, &height);
		if (width == 0 || height == 0) return;

		VLKCHECK(vkDeviceWaitIdle(app.device));

		cleanupSwapChain();

		{
			auto descSetLayout = app.swapChain.descriptorSetLayout;
			auto descPool = app.swapChain.descriptorPool;
			auto pipelineLayout = app.swapChain.pipelineLayout;
			app.swapChain = createSwapChain(app);
			app.swapChain.imageViews = createSwapChainImageViews(app);
			app.swapChain.descriptorSetLayout = descSetLayout;
			app.swapChain.descriptorPool = descPool;
			app.swapChain.pipelineLayout = pipelineLayout;
		}

		{
			app.gBuffer.createAttachments(app);
			app.gBuffer.renderPass = createGeometryRenderPass(app);
			app.gBuffer.framebuffer = createGBufferFramebuffer(app);
			//app.gBuffer = createGBuffer(app, gBufAttachments, geomRenderPass);
			app.gBuffer.pipeline = createGBufferPipeline(app);
		}

		{
			app.swapChain.depthImage = createDepthImage(app);

			const auto lightRenderPass = createLightingRenderPass(app);
			app.swapChain.renderPass = lightRenderPass;
			app.swapChain.framebuffers = createSwapChainFramebuffers(app);
			app.swapChain.pipeline = createSwapChainPipeline(app, isDebug ? "3d" : "composition");

			if (isDebug) {
				app.swapChain.descriptorSet = createSwapChainDebugDescriptorSet(app,
						app.swapChain.descriptorSetLayout,
						mvpUniformBuffer, texDiffuseImage);
				swapCommandBuffers = createSwapChainDebugCommandBuffers(app, nIndices,
					vertexBuffer, indexBuffer,
					mvpUniformBuffer, app.swapChain.descriptorSet);
			} else {
				app.swapChain.descriptorSet = createSwapChainDescriptorSet(app,
						app.swapChain.descriptorSetLayout,
						compUniformBuffer, texDiffuseImage);
				swapCommandBuffers = createSwapChainCommandBuffers(app, nIndices,
					compUniformBuffer, app.swapChain.descriptorSet);
			}
		}

		app.gBuffer.descriptorSet = createGBufferDescriptorSet(app, app.gBuffer.descriptorSetLayout,
				mvpUniformBuffer, texDiffuseImage, texSpecularImage);
		gbufCommandBuffer = createGBufferCommandBuffer(app, nIndices,
				vertexBuffer, indexBuffer, mvpUniformBuffer, app.gBuffer.descriptorSet);

	}

	void createSemaphores() {
		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VLKCHECK(vkCreateSemaphore(app.device, &semaphoreInfo, nullptr, &imageAvailableSemaphore));
		VLKCHECK(vkCreateSemaphore(app.device, &semaphoreInfo, nullptr, &renderFinishedSemaphore));
		VLKCHECK(vkCreateSemaphore(app.device, &semaphoreInfo, nullptr, &gBufRenderFinishedSemaphore));
	}

	void drawFrame2() {
		const auto imageIndex = acquireNextSwapImage(app, imageAvailableSemaphore);
		if (imageIndex < 0) {
			recreateSwapChain();
			return;
		}

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
		VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &swapCommandBuffers[imageIndex];

		VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(app.queues.graphics, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
			throw std::runtime_error("failed to submit draw command buffer!");
		}

		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapChains[] = {app.swapChain.handle};
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;

		presentInfo.pImageIndices = &imageIndex;

		auto result = vkQueuePresentKHR(app.queues.present, &presentInfo);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			recreateSwapChain();
		} else if (result != VK_SUCCESS) {
			throw std::runtime_error("failed to present swap chain image!");
		}

		vkQueueWaitIdle(app.queues.present);
	}

	void drawFrame() {
		const auto imageIndex = acquireNextSwapImage(app, imageAvailableSemaphore);
		if (imageIndex < 0) {
			recreateSwapChain();
			return;
		}

		drawGBuffer();
		drawSwap(imageIndex);

		submitFrame(imageIndex);
	}

	void drawGBuffer() {
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
		submitInfo.pCommandBuffers = &gbufCommandBuffer;

		// Signal semaphore when done
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &gBufRenderFinishedSemaphore;

		VLKCHECK(vkQueueSubmit(app.queues.graphics, 1, &submitInfo, VK_NULL_HANDLE));
	}

	void drawSwap(uint32_t imageIndex) {
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		// Wait for G-buffer
		const std::array<VkPipelineStageFlags, 1> waitStages = {
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		};
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &gBufRenderFinishedSemaphore;
		submitInfo.pWaitDstStageMask = waitStages.data();

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &swapCommandBuffers[imageIndex];

		// Signal once done
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

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			recreateSwapChain();
		else if (result != VK_SUCCESS)
			throw std::runtime_error("failed to present swap chain image!");

		VLKCHECK(vkQueueWaitIdle(app.queues.graphics));
	}


	/*
	void updateVertexBuffer() {
		// Acquire handle to device memory
		void *data;
		vkMapMemory(app.device, vertexBuffer.memory, 0, VERTEX_BUFFER_SIZE, 0, &data);
		// Copy host memory to device
		memcpy(data, streamingBufferData, VERTEX_BUFFER_SIZE);
		vkUnmapMemory(app.device, vertexBuffer.memory);
	}

	void updateIndexBuffer() {
		// Acquire handle to device memory
		void *data;
		vkMapMemory(app.device, indexBuffer.memory, 0, INDEX_BUFFER_SIZE, 0, &data);
		// Copy host memory to device
		memcpy(data, streamingBufferData + VERTEX_BUFFER_SIZE, INDEX_BUFFER_SIZE);
		vkUnmapMemory(app.device, indexBuffer.memory);
	}*/

	void updateMVPUniformBuffer() {
		static auto startTime = std::chrono::high_resolution_clock::now();

		MVPUniformBufferObject ubo = {};

		if (useCamera) {
			ubo.model = glm::mat4{1.0f};
			ubo.view = camera.viewMatrix();
		} else {
			auto currentTime = std::chrono::high_resolution_clock::now();
			float time = std::chrono::duration<float, std::chrono::seconds::period>(
					currentTime - startTime).count();
			ubo.model = glm::rotate(glm::mat4{1.0f}, time * glm::radians(90.f), glm::vec3{0.f, -1.f, 0.f});
			ubo.view = glm::lookAt(glm::vec3{140,140,140},glm::vec3{0,0,0},glm::vec3{0,1,0});
		}
		ubo.proj = glm::perspective(glm::radians(60.f),
				app.swapChain.extent.width / float(app.swapChain.extent.height), 0.1f, 300.f);
		ubo.proj[1][1] *= -1;

		void *data;
		vkMapMemory(app.device, mvpUniformBuffer.memory, 0, sizeof(ubo), 0, &data);
		memcpy(data, &ubo, sizeof(ubo));
		vkUnmapMemory(app.device, mvpUniformBuffer.memory);
	}

	void updateCompUniformBuffer() {
		CompositionUniformBufferObject ubo = {};
		ubo.viewPos = glm::vec4{ camera.position.x, camera.position.y, camera.position.z, 0 };

		void *data;
		vkMapMemory(app.device, compUniformBuffer.memory, 0, sizeof(ubo), 0, &data);
		memcpy(data, &ubo, sizeof(ubo));
		vkUnmapMemory(app.device, compUniformBuffer.memory);
	}


	void prepareBufferMemory() {
		// Prepare buffer memory
		streamingBufferData = new uint8_t[VERTEX_BUFFER_SIZE + INDEX_BUFFER_SIZE];

		vertexBuffer = createBuffer(app, VERTEX_BUFFER_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		indexBuffer = createBuffer(app, INDEX_BUFFER_SIZE, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		mvpUniformBuffer = createBuffer(app, sizeof(MVPUniformBufferObject),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		compUniformBuffer = createBuffer(app, sizeof(CompositionUniformBufferObject),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		// Map vertex/index device memory to host
		vkMapMemory(app.device, vertexBuffer.memory, 0, VERTEX_BUFFER_SIZE, 0, &vertexData);
		vkMapMemory(app.device, indexBuffer.memory, 0, INDEX_BUFFER_SIZE, 0, &indexData);
	}


	void prepareCamera() {
		// Prepare camera
		camera = createCamera();
		cameraCtrl = std::make_unique<CameraController>(camera);
		activeEP.setCamera(&camera);
	}
};

int main(int argc, char **argv) {
	if (!Endpoint::init()) {
		std::cerr << "Failed to initialize sockets." << std::endl;
		return EXIT_FAILURE;
	}
	if (!xplatEnableExitHandler()) {
		std::cerr << "Failed to enable exit handler!" << std::endl;
		return EXIT_FAILURE;
	}
	xplatSetExitHandler([] () {
		if (Endpoint::cleanup())
			std::cerr << "Successfully cleaned up sockets." << std::endl;
		else
			std::cerr << "Failed to cleanup sockets!" << std::endl;
	});

	// Parse args
	int i = argc - 1;
	while (i > 0) {
		if (strlen(argv[i]) < 2) {
			std::cerr << "Invalid flag: -\n";
			return EXIT_FAILURE;
		}
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'd': isDebug = true; break;
			case 'c': useCamera = true; break;
			default:
				std::cout << "Usage: " << argv[0] << " [-c (use camera)] [-d (debug mode, aka use forward rendering)]\n";
				break;
			}
		}
		--i;
	}

	HelloTriangleApplication app;

	try {
		app.run();
	} catch (const std::runtime_error& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
