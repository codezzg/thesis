#pragma once

#include "application.hpp"
#include "buffer_array.hpp"
#include "camera.hpp"
#include "camera_ctrl.hpp"
#include "client_endpoint.hpp"
#include "client_resources.hpp"
#include "fps_counter.hpp"
#include "geometry.hpp"
#include "network_data.hpp"
#include "shader_opts.hpp"
#include <memory>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>

class VulkanClient final {
public:
	void run(const char* ip);

	void disconnect();

private:
	friend void cbCursorMoved(GLFWwindow* window, double xpos, double ypos);
	friend void cbKeyPressed(GLFWwindow* window, int key, int /*scancode*/, int action, int);

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

	/** Maps objId => world transform.
	 *  Contains models and pointLights
	 */
	ObjectTransforms objTransforms;

	/** Memory area staging vertices and indices coming from the server */
	std::vector<uint8_t> streamingBuffer;
	/** Memory area staging update requests read from the raw server data */
	std::vector<UpdateReq> updateReqs;

	Camera camera;
	std::unique_ptr<CameraController> cameraCtrl;

	/** Contains togglable debug options for shaders */
	ShaderOpts shaderOpts;

	void initVulkan();

	void startNetwork(const char* serverIp);

	bool connectToServer(const char* serverIp);

	/** Check we received all the resources needed by all models */
	void checkAssets(const ClientTmpResources& resources);

	bool loadAssets(const ClientTmpResources& resources);

	void mainLoop(const char* serverIp);

	void runFrame();

	void calcTimeStats(FPSCounter& fps, std::chrono::time_point<std::chrono::high_resolution_clock>& beginTime);

	void recreateSwapChain();

	void createSemaphores();

	void drawFrame();

	void renderFrame(uint32_t imageIndex);

	void submitFrame(uint32_t imageIndex);

	void updateObjectsUniformBuffer();

	void updateViewUniformBuffer();

	void prepareBufferMemory(Buffer& stagingBuffer);

	void prepareCamera();

	void loadSkybox();

	void recordAllCommandBuffers();

	void createDescriptorSets();

	void cleanupSwapChain();

	void cleanup();
};
