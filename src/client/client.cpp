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
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <chrono>
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
#include "FPSCounter.hpp"
#include "vertex.hpp"
#include "validation.hpp"
#include "vulk_utils.hpp"
#include "config.hpp"
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

// Fuck off, Windows
#undef max
#undef min

using namespace std::literals::string_literals;

struct UniformBufferObject final {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

class HelloTriangleApplication final {
public:
	void run() {
		app.init();

		glfwSetWindowUserPointer(app.window, this);
		glfwSetWindowSizeCallback(app.window, onWindowResized);
		glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		glfwSetCursorPosCallback(app.window, cursorPosCallback);

		initVulkan();
		mainLoop();
		cleanup();
	}

private:
	Application app;

	ClientPassiveEndpoint passiveEP;
	ClientActiveEndpoint activeEP;
	int64_t curFrame = -1;

	Camera camera;
	std::unique_ptr<CameraController> cameraCtrl;

	VkPipelineLayout pipelineLayout;
	VkPipeline graphicsPipeline;

	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	std::vector<VkCommandBuffer> commandBuffers;

	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;

	Buffer vertexBuffer;
	Buffer indexBuffer;
	Buffer uniformBuffer;

	uint8_t *streamingBufferData = nullptr;
	uint64_t nVertices = 0;
	uint64_t nIndices = 0;
	static constexpr size_t VERTEX_BUFFER_SIZE = 1<<24;
	static constexpr size_t INDEX_BUFFER_SIZE = 1<<24;
	static constexpr size_t UNIFORM_BUFFER_SIZE = sizeof(UniformBufferObject);


	void initVulkan() {
		app.memory.reserve(VERTEX_BUFFER_SIZE + INDEX_BUFFER_SIZE + UNIFORM_BUFFER_SIZE);

		app.swapChain = createSwapChain(app);
		createSwapChainImageViews(app);
		app.renderPass = createRenderPass(app);
		createDescriptorSetLayout();
		createGraphicsPipeline();
		app.commandPool = createCommandPool(app.device, app.physicalDevice, app.surface);
		createDepthResources();
		createSwapChainFramebuffers(app);
		createTextureImage();
		createTextureSampler();

		//vertices = VERTICES;
		//indices = INDICES;
		streamingBufferData = reinterpret_cast<uint8_t*>(malloc(VERTEX_BUFFER_SIZE + INDEX_BUFFER_SIZE));
		camera = createCamera();
		cameraCtrl = std::make_unique<CameraController>(camera);
		activeEP.setCamera(&camera);
		//loadModel(cfg::MODEL_PATH, vertices, indices);

		vertexBuffer = createBuffer(app, VERTEX_BUFFER_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		indexBuffer = createBuffer(app, INDEX_BUFFER_SIZE, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		uniformBuffer = createBuffer(app, UNIFORM_BUFFER_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		createDescriptorPool();
		createDescriptorSet();
		createCommandBuffers();
		createSemaphores();
	}

	void mainLoop() {
		passiveEP.startPassive(cfg::CLIENT_PASSIVE_IP, cfg::CLIENT_PASSIVE_PORT);
		passiveEP.runLoop();

		activeEP.startActive(cfg::CLIENT_ACTIVE_IP, cfg::CLIENT_ACTIVE_PORT);
		activeEP.runLoop();

		FPSCounter fps;
		fps.start();

		updateVertexBuffer();
		updateIndexBuffer();
		updateUniformBuffer();

		auto beginTime = std::chrono::high_resolution_clock::now();
		auto& clock = Clock::instance();
		while (!glfwWindowShouldClose(app.window)) {
			glfwPollEvents();

			runFrame();

			// Time calculation and stuff
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

		passiveEP.close();
		vkDeviceWaitIdle(app.device);
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
			vkFreeCommandBuffers(app.device, app.commandPool, static_cast<uint32_t>(commandBuffers.size()),
				commandBuffers.data());
			createCommandBuffers();
		}

		updateVertexBuffer();
		updateIndexBuffer();
		updateUniformBuffer();

		cameraCtrl->processInput(app.window);

		drawFrame();
	}

	// TODO
	void receiveData() {
		//std::cerr << "receive data. curFrame = " << curFrame << ", passive.get = " << passiveEP.getFrameId() << "\n";
		if (curFrame >= 0 && passiveEP.getFrameId() == curFrame)
			return;

		const auto data = passiveEP.peek();
		//std::cerr << "data = " << data << "\n";
		if (data == nullptr)
			return;

		curFrame = passiveEP.getFrameId();

		// data is [(64b)nVertices|(64b)nIndices|vertices|indices]
		nVertices = *reinterpret_cast<const uint64_t*>(data);
		nIndices = *(reinterpret_cast<const uint64_t*>(data) + 1);
		printf("\nn vertices: %lu, n indices: %lu\n", nVertices, nIndices);
		//for (size_t i = 0; i < nVertices; ++i)
			//std::cerr << "v[" << i << "] = "
				//<< *((Vertex*)(data + 20 + sizeof(Vertex)*i)) << std::endl;

		//vertices.resize(nVertices);
		//const auto vOff = 2 * sizeof(uint64_t);
		//for (unsigned i = 0; i < nVertices; ++i)
			//vertices[i] = *(Vertex*)(data + vOff + i * sizeof(Vertex));
		//std::cerr << "begin vertices\n";
		//[>for (auto& v : vertices) {
			//std::cerr << v << std::endl;
			////v.pos.x += 0.001;
			////if (v.pos.x > 1) v.pos.x = 0;
		//}*/
		//std::cerr << "end vertices (" << vertices.size() << ")\n";

		//indices.resize(nIndices);
		////memcpy(indices.data(), data + 28 + nVertices * sizeof(Vertex), nIndices * sizeof(Index));
		//const auto iOff = vOff + nVertices * sizeof(Vertex);
		//std::cerr << "iOff = " << iOff << "\n";
		//for (unsigned i = 0; i < nIndices; ++i)
			//indices[i] = *(Index*)(data + iOff + i * sizeof(Index));

		//std::cerr << "begin indices\n";
		//[>for (auto& i : indices) {
			//std::cerr << i << ", ";
		//}*/
		//std::cerr << "\nend indices (" << indices.size() << ")\n";

		//printf("[%ld] raw data:\n", curFrame);
		//for (int i = 0; i < 150; ++i) {
			//if (i == vOff) printf("|> ");
			//if (i == iOff) printf("<| ");
			//printf("%hhx ", data[i]);
		//}
		//printf("\n");


		constexpr auto HEADER_SIZE = 2 * sizeof(uint64_t);
		memcpy(streamingBufferData, data + HEADER_SIZE, nVertices * sizeof(Vertex));
		memcpy(streamingBufferData + VERTEX_BUFFER_SIZE, data + HEADER_SIZE + nVertices * sizeof(Vertex),
				nIndices * sizeof(Index));

		//if (curFrame == 1) {
			//std::ofstream of("sb.data", std::ios::binary);
			//for (int i = 0; i < vertices.size() * sizeof(Vertex) + indices.size() * sizeof(Index); ++i)
				//of << ((uint8_t*)streamingBufferData)[i];
		//}
	}

	void cleanupSwapChain() {
		vkDestroyImageView(app.device, app.depthImage.view, nullptr);
		vkDestroyImage(app.device, app.depthImage.handle, nullptr);
		vkFreeMemory(app.device, app.depthImage.memory, nullptr);

		for (auto framebuffer : app.swapChain.framebuffers) {
			vkDestroyFramebuffer(app.device, framebuffer, nullptr);
		}

		vkFreeCommandBuffers(app.device, app.commandPool, static_cast<uint32_t>(commandBuffers.size()),
				commandBuffers.data());

		vkDestroyPipeline(app.device, graphicsPipeline, nullptr);
		vkDestroyPipelineLayout(app.device, pipelineLayout, nullptr);
		vkDestroyRenderPass(app.device, app.renderPass, nullptr);

		for (auto imageView : app.swapChain.imageViews)
			vkDestroyImageView(app.device, imageView, nullptr);

		vkDestroySwapchainKHR(app.device, app.swapChain.handle, nullptr);
	}

	void cleanup() {
		cleanupSwapChain();

		vkDestroySampler(app.device, app.textureImage.sampler, nullptr);
		vkDestroyImageView(app.device, app.textureImage.view, nullptr);
		vkDestroyImage(app.device, app.textureImage.handle, nullptr);
		vkFreeMemory(app.device, app.textureImage.memory, nullptr);

		vkDestroyDescriptorPool(app.device, descriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(app.device, descriptorSetLayout, nullptr);

		vkDestroyBuffer(app.device, uniformBuffer.handle, nullptr);
		vkFreeMemory(app.device, uniformBuffer.memory, nullptr);
		vkDestroyBuffer(app.device, indexBuffer.handle, nullptr);
		vkFreeMemory(app.device, indexBuffer.memory, nullptr);
		vkDestroyBuffer(app.device, vertexBuffer.handle, nullptr);
		vkFreeMemory(app.device, vertexBuffer.memory, nullptr);

		vkDestroySemaphore(app.device, renderFinishedSemaphore, nullptr);
		vkDestroySemaphore(app.device, imageAvailableSemaphore, nullptr);
		vkDestroyCommandPool(app.device, app.commandPool, nullptr);

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

		app.swapChain = createSwapChain(app);
		createSwapChainImageViews(app);
		app.renderPass = createRenderPass(app);
		createGraphicsPipeline();
		createDepthResources();
		createSwapChainFramebuffers(app);
		createCommandBuffers();
	}

	void createDescriptorSetLayout() {
		VkDescriptorSetLayoutBinding uboLayoutBinding = {};
		uboLayoutBinding.binding = 0;
		uboLayoutBinding.descriptorCount = 1;
		uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboLayoutBinding.pImmutableSamplers = nullptr;
		uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
		samplerLayoutBinding.binding = 1;
		samplerLayoutBinding.descriptorCount = 1;
		samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerLayoutBinding.pImmutableSamplers = nullptr;
		samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
			uboLayoutBinding,
			samplerLayoutBinding
		};

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = bindings.size();
		layoutInfo.pBindings = bindings.data();

		VLKCHECK(vkCreateDescriptorSetLayout(app.device, &layoutInfo, nullptr, &descriptorSetLayout));
	}

	void createGraphicsPipeline() {
		auto vertShaderCode = readFile("shaders/3d.vert.spv");
		auto fragShaderCode = readFile("shaders/3d.frag.spv");

		auto vertShaderModule = createShaderModule(vertShaderCode);
		auto fragShaderModule = createShaderModule(fragShaderCode);

		VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo shaderStages[] = {
			vertShaderStageInfo,
			fragShaderStageInfo
		};

		// Configure fixed pipeline
		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		auto bindingDescription = Vertex::getBindingDescription();
		auto attributeDescriptions = Vertex::getAttributeDescriptions();
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size();
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkViewport viewport = {};
		viewport.x = 0.f;
		viewport.y = 0.f;
		viewport.width = static_cast<float>(app.swapChain.extent.width);
		viewport.height = static_cast<float>(app.swapChain.extent.height);
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;

		VkRect2D scissor = {};
		scissor.offset = {0, 0};
		scissor.extent = app.swapChain.extent;

		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.f;
		rasterizer.depthBiasClamp = 0.f;
		rasterizer.depthBiasSlopeFactor = 0.f;

		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
						| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

		VkPipelineColorBlendStateCreateInfo colorBlending = {};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		VkPipelineDepthStencilStateCreateInfo depthStencil = {};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = nullptr;

		VLKCHECK(vkCreatePipelineLayout(app.device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

		VkGraphicsPipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pDynamicState = nullptr;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = app.renderPass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
		pipelineInfo.basePipelineIndex = -1;

		VLKCHECK(vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1,
					&pipelineInfo, nullptr, &graphicsPipeline));

		// Cleanup
		vkDestroyShaderModule(app.device, fragShaderModule, nullptr);
		vkDestroyShaderModule(app.device, vertShaderModule, nullptr);
	}

	void createDepthResources() {
		const auto depthFormat = findDepthFormat(app.physicalDevice);
		app.depthImage = createImage(app, app.swapChain.extent.width, app.swapChain.extent.height, depthFormat,
				VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		app.depthImage.view = createImageView(app, app.depthImage.handle,
				depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

		transitionImageLayout(app, app.depthImage.handle, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}

	void createTextureImage() {
		int texWidth, texHeight, texChannels;
		auto pixels = stbi_load(cfg::TEXTURE_PATH, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		VkDeviceSize imageSize = texWidth * texHeight * 4;

		if (!pixels)
			throw std::runtime_error("failed to load texture image!");

		const auto stagingBuffer = createBuffer(app, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		void *data;
		vkMapMemory(app.device, stagingBuffer.memory, 0, imageSize, 0, &data);
		memcpy(data, pixels, static_cast<size_t>(imageSize));
		vkUnmapMemory(app.device, stagingBuffer.memory);

		stbi_image_free(pixels);

		app.textureImage = createImage(app, texWidth, texHeight,
				VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		app.textureImage.view = createImageView(app, app.textureImage.handle,
				VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
		transitionImageLayout(app, app.textureImage.handle, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		copyBufferToImage(app, stagingBuffer.handle, app.textureImage.handle, texWidth, texHeight);
		transitionImageLayout(app, app.textureImage.handle, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		vkDestroyBuffer(app.device, stagingBuffer.handle, nullptr);
		vkFreeMemory(app.device, stagingBuffer.memory, nullptr);
	}

	void createTextureSampler() {
		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.maxAnisotropy = 16;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

		VLKCHECK(vkCreateSampler(app.device, &samplerInfo, nullptr, &app.textureImage.sampler));
	}

	void createDescriptorPool() {
		std::array<VkDescriptorPoolSize, 2> poolSizes = {};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = 1;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = 1;

		VkDescriptorPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = poolSizes.size();
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = 1;

		VLKCHECK(vkCreateDescriptorPool(app.device, &poolInfo, nullptr, &descriptorPool));
	}

	void createDescriptorSet() {
		VkDescriptorSetLayout layouts[] = { descriptorSetLayout };
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = layouts;

		VLKCHECK(vkAllocateDescriptorSets(app.device, &allocInfo, &descriptorSet));

		VkDescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = uniformBuffer.handle;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(UniformBufferObject);

		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = app.textureImage.view;
		imageInfo.sampler = app.textureImage.sampler;

		std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};
		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = descriptorSet;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pBufferInfo = &bufferInfo;

		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstSet = descriptorSet;
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].dstArrayElement = 0;
		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(app.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
	}

	void createCommandBuffers() {
		commandBuffers.resize(app.swapChain.framebuffers.size());

		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = app.commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

		VLKCHECK(vkAllocateCommandBuffers(app.device, &allocInfo, commandBuffers.data()));

		for (size_t i = 0; i < commandBuffers.size(); ++i) {
			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
			beginInfo.pInheritanceInfo = nullptr;

			vkBeginCommandBuffer(commandBuffers[i], &beginInfo);

			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = app.renderPass;
			renderPassInfo.framebuffer = app.swapChain.framebuffers[i];
			renderPassInfo.renderArea.offset = {0, 0};
			renderPassInfo.renderArea.extent = app.swapChain.extent;
			std::array<VkClearValue, 2> clearValues = {};
			clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
			clearValues[1].depthStencil = {1.f, 0};
			renderPassInfo.clearValueCount = clearValues.size();
			renderPassInfo.pClearValues = clearValues.data();

			vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
			VkBuffer vertexBuffers[] = { vertexBuffer.handle };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
					pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
			vkCmdDrawIndexed(commandBuffers[i], nIndices, 1, 0, 0, 0);
			std::cerr << "recreating command buffer with v = "
				<< nVertices << ", i = " << nIndices << "\n";
			vkCmdEndRenderPass(commandBuffers[i]);

			VLKCHECK(vkEndCommandBuffer(commandBuffers[i]));
		}
	}

	void createSemaphores() {
		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		if (vkCreateSemaphore(app.device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS
			|| vkCreateSemaphore(app.device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS)
			throw std::runtime_error("failed to create semaphores!");
	}

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
	}

	void updateUniformBuffer() {
		static auto startTime = std::chrono::high_resolution_clock::now();

		auto currentTime = std::chrono::high_resolution_clock::now();
		float time = std::chrono::duration<float, std::chrono::seconds::period>(
				currentTime - startTime).count();

		UniformBufferObject ubo = {};
		ubo.model = glm::mat4{1.0f};
		//ubo.model = glm::rotate(glm::mat4{1.0f}, time * glm::radians(90.f), glm::vec3{0.f, -1.f, 0.f});
		//std::cerr << "view mat = " << glm::to_string(camera.viewMatrix()) << "\n";
		ubo.view = camera.viewMatrix();
			//glm::lookAt(glm::vec3{140,140,140},glm::vec3{0,0,0},glm::vec3{0,1,0});
		ubo.proj = glm::perspective(glm::radians(60.f),
				app.swapChain.extent.width / float(app.swapChain.extent.height), 0.1f, 300.f);
		ubo.proj[1][1] *= -1;

		void *data;
		vkMapMemory(app.device, uniformBuffer.memory, 0, sizeof(ubo), 0, &data);
		memcpy(data, &ubo, sizeof(ubo));
		vkUnmapMemory(app.device, uniformBuffer.memory);
	}


	void drawFrame() {

		uint32_t imageIndex;
		auto result = vkAcquireNextImageKHR(app.device, app.swapChain.handle,
				std::numeric_limits<uint64_t>::max(),
				imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			recreateSwapChain();
			return;
		} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			throw std::runtime_error("failed to acquire swap chain image!");
		}

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
		VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		VLKCHECK(vkQueueSubmit(app.queues.graphics, 1, &submitInfo, VK_NULL_HANDLE));

		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;
		VkSwapchainKHR swapChains[] = { app.swapChain.handle };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.pResults = nullptr;

		result = vkQueuePresentKHR(app.queues.present, &presentInfo);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			recreateSwapChain();
		else if (result != VK_SUCCESS)
			throw std::runtime_error("failed to present swap chain image!");

		vkQueueWaitIdle(app.queues.present);
	}

	VkShaderModule createShaderModule(const std::vector<char>& code) const {
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		VLKCHECK(vkCreateShaderModule(app.device, &createInfo, nullptr, &shaderModule));

		return shaderModule;
	}
};

int main() {
	if (!Endpoint::init()) {
		std::cerr << "Failed to initialize sockets." << std::endl;
		return EXIT_FAILURE;
	}
	std::atexit([]() { Endpoint::cleanup(); });

	HelloTriangleApplication app;

	try {
		app.run();
	} catch (const std::runtime_error& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
