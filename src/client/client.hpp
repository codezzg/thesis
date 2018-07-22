#pragma once

#include "application.hpp"
#include "buffer_array.hpp"
#include "camera.hpp"
#include "camera_ctrl.hpp"
#include "cf_hashset.hpp"
#include "client_tcp.hpp"
#include "client_udp.hpp"
#include "endpoint.hpp"
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
	~VulkanClient();

	void run(const char* ip);

	void disconnect();

	void reqModel(uint16_t n);

private:
	friend void cbCursorMoved(GLFWwindow* window, double xpos, double ypos);
	friend void cbKeyPressed(GLFWwindow* window, int key, int, int action, int);

	Application app;

	bool fullscreen = false;

	struct {
		Endpoint active;
		Endpoint passive;
		Endpoint reliable;
	} endpoints;

	struct {
		std::unique_ptr<UdpActiveThread> udpActive;
		std::unique_ptr<UdpPassiveThread> udpPassive;
		std::unique_ptr<TcpMsgThread> tcpMsg;
		std::unique_ptr<KeepaliveThread> keepalive;
	} networkThreads;

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

	Buffer stagingBuffer;

	/** Stores resources received via network */
	NetworkResources netRsrc;

	/** Maps objId => world transform.
	 *  Contains models and pointLights
	 */
	ObjectTransforms objTransforms;

	/** Memory area staging vertices and indices coming from the server */
	std::vector<uint8_t> streamingBuffer;
	/** Update requests read from the raw server data */
	std::vector<UpdateReq> updateReqs;

	/** Set of GeomUpdate serialIds that we already received. If a chunk with the same
	 *  serialId is received, we ignore it.
	 */
	cf::hashset<uint32_t> receivedGeomIds;
	/** Backing memory for `receivedGeomIds` */
	void* receivedGeomIdsMem = nullptr;
	std::size_t receivedGeomIdsMemSize = 0;

	/** List of UDP acks to send to the server */
	std::vector<uint32_t> acksToSend;

	/** Map { missingTex => [{ texType, material that need it }] } */
	enum class TextureType { DIFFUSE, SPECULAR, NORMAL };
	std::unordered_map<StringId, std::vector<std::pair<TextureType, StringId>>> missingTextures;

	Camera camera;
	std::unique_ptr<CameraController> cameraCtrl;

	/** Contains togglable debug options for shaders */
	ShaderOpts shaderOpts;

	/** Creates all the permanent Vulkan resources.
	 *  Those are freed by cleanup().
	 */
	void initVulkan();

	/** Starts the UDP network endpoints */
	void startUdp(const char* serverIp);

	/** Performs the initial handshake with the server and receives the one-time data */
	bool connectToServer(const char* serverIp);

	/** Check we received all the resources needed by all models */
	void collectMissingTextures(const ClientTmpResources& resources);

	/** Takes the raw resources received by the server and processes them into usable resources */
	bool loadAssets(const ClientTmpResources& resources,
		/* out */ std::vector<ModelInfo>& newModels,
		/* out */ std::vector<Material>& newMaterials,
		/* out */ std::vector<StringId>& newTextures);

	void prepareReceivedGeomHashset();
	/** Creates the buffers that will stay alive until cleanup */
	void createPermanentBuffers(Buffer& stagingBuffer);
	void createSemaphores();
	void createPermanentDescriptorSets();
	void prepareCamera();
	void loadSkybox();

	void mainLoop();

	void runFrame();

	void applyUpdateRequests();
	void recreateResources(const std::vector<ModelInfo>& newModels,
		const std::vector<Material>& newMaterials,
		const std::vector<StringId>& newTextures);
	void regenMaterials(const std::vector<StringId>& newTextures);

	void calcTimeStats(FPSCounter& fps, std::chrono::time_point<std::chrono::high_resolution_clock>& beginTime);

	void recreateSwapChain();
	/** Creates or updates the buffers which get regenerated every time new resources arrive */
	void updateBuffers();

	void drawFrame();
	void renderFrame(uint32_t imageIndex);
	void submitFrame(uint32_t imageIndex);

	void updateObjectsUniformBuffer();
	void updateViewUniformBuffer();
	void updateLightsUniformBuffer();

	void recordAllCommandBuffers();

	/** Releases the resources related to the current swap chain */
	void cleanupSwapChain();

	/** Releases all the resources */
	void cleanup();
};
