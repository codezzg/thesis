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
#include "model.hpp"
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

	VkSwapchainKHR swapChain;
	std::vector<VkImage> swapChainImages;
	VkFormat swapChainImageFormat;
	VkExtent2D swapChainExtent;
	std::vector<VkImageView> swapChainImageViews;
	std::vector<VkFramebuffer> swapChainFramebuffers;

	VkPipelineLayout pipelineLayout;
	VkRenderPass renderPass;
	VkPipeline graphicsPipeline;

	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	std::vector<VkCommandBuffer> commandBuffers;

	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexBufferMemory;
	VkBuffer indexBuffer;
	VkDeviceMemory indexBufferMemory;
	VkBuffer uniformBuffer;
	VkDeviceMemory uniformBufferMemory;

	void *streamingBufferData = nullptr;
	static constexpr size_t VERTEX_BUFFER_SIZE = 1<<24;
	static constexpr size_t INDEX_BUFFER_SIZE = 1<<24;

	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
	VkSampler textureSampler;

	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;


	void initVulkan() {
		createSwapChain();
		createImageViews();
		createRenderPass();
		createDescriptorSetLayout();
		createGraphicsPipeline();
		app.commandPool = createCommandPool(app.device, app.physicalDevice, app.surface);
		createDepthResources();
		createFramebuffers();
		createTextureImage();
		createTextureImageView();
		createTextureSampler();

		//vertices = VERTICES;
		//indices = INDICES;
		streamingBufferData = malloc(VERTEX_BUFFER_SIZE + INDEX_BUFFER_SIZE);
		camera = createCamera();
		cameraCtrl = std::make_unique<CameraController>(camera);
		activeEP.setCamera(&camera);
		//loadModel(cfg::MODEL_PATH, vertices, indices);

		createVertexBuffer();
		createIndexBuffer();
		createUniformBuffer();
		createDescriptorPool();
		createDescriptorSet();
		createCommandBuffers();
		createSemaphores();
	}

	void mainLoop() {
		passiveEP.startPassive("0.0.0.0", 1234);
		passiveEP.runLoop();

		activeEP.startActive("0.0.0.0", 1235);
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
		static size_t pvs = vertices.size(),
		              pis = indices.size();

		// Receive network data
		receiveData(vertices, indices);

		if (vertices.size() != pvs || indices.size() != pis) {
			pvs = vertices.size();
			pis = indices.size();
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
	void receiveData(std::vector<Vertex>& vertices, std::vector<Index>& indices) {
		//std::cerr << "receive data. curFrame = " << curFrame << ", passive.get = " << passiveEP.getFrameId() << "\n";
		if (curFrame >= 0 && passiveEP.getFrameId() == curFrame)
			return;

		const auto data = passiveEP.peek();
		//std::cerr << "data = " << data << "\n";
		if (data == nullptr)
			return;

		curFrame = passiveEP.getFrameId();

		// data is [(64b)nVertices|(64b)nIndices|vertices|indices]
		uint64_t nVertices = *reinterpret_cast<const uint64_t*>(data);
		uint64_t nIndices = *(reinterpret_cast<const uint64_t*>(data) + 1);
		printf("\nn vertices: %lu, n indices: %lu\n", nVertices, nIndices);
		//for (size_t i = 0; i < nVertices; ++i)
			//std::cerr << "v[" << i << "] = "
				//<< *((Vertex*)(data + 20 + sizeof(Vertex)*i)) << std::endl;

		vertices.resize(nVertices);
		const auto vOff = 2 * sizeof(uint64_t);
		for (unsigned i = 0; i < nVertices; ++i)
			vertices[i] = *(Vertex*)(data + vOff + i * sizeof(Vertex));
		std::cerr << "begin vertices\n";
		/*for (auto& v : vertices) {
			std::cerr << v << std::endl;
			//v.pos.x += 0.001;
			//if (v.pos.x > 1) v.pos.x = 0;
		}*/
		std::cerr << "end vertices (" << vertices.size() << ")\n";

		indices.resize(nIndices);
		//memcpy(indices.data(), data + 28 + nVertices * sizeof(Vertex), nIndices * sizeof(Index));
		const auto iOff = vOff + nVertices * sizeof(Vertex);
		std::cerr << "iOff = " << iOff << "\n";
		for (unsigned i = 0; i < nIndices; ++i)
			indices[i] = *(Index*)(data + iOff + i * sizeof(Index));

		std::cerr << "begin indices\n";
		/*for (auto& i : indices) {
			std::cerr << i << ", ";
		}*/
		std::cerr << "\nend indices (" << indices.size() << ")\n";

		printf("[%ld] raw data:\n", curFrame);
		for (int i = 0; i < 150; ++i) {
			if (i == vOff) printf("|> ");
			if (i == iOff) printf("<| ");
			printf("%hhx ", data[i]);
		}
		printf("\n");


		memcpy(streamingBufferData, vertices.data(), vertices.size() * sizeof(Vertex));
		memcpy((uint8_t*)streamingBufferData + VERTEX_BUFFER_SIZE,
				indices.data(), indices.size() * sizeof(Index));

		if (curFrame == 1) {
			std::ofstream of("sb.data", std::ios::binary);
			for (int i = 0; i < vertices.size() * sizeof(Vertex) + indices.size() * sizeof(Index); ++i)
				of << ((uint8_t*)streamingBufferData)[i];
		}
	}

	void cleanupSwapChain() {
		vkDestroyImageView(app.device, depthImageView, nullptr);
		vkDestroyImage(app.device, depthImage, nullptr);
		vkFreeMemory(app.device, depthImageMemory, nullptr);

		for (auto framebuffer : swapChainFramebuffers) {
			vkDestroyFramebuffer(app.device, framebuffer, nullptr);
		}

		vkFreeCommandBuffers(app.device, app.commandPool, static_cast<uint32_t>(commandBuffers.size()),
				commandBuffers.data());

		vkDestroyPipeline(app.device, graphicsPipeline, nullptr);
		vkDestroyPipelineLayout(app.device, pipelineLayout, nullptr);
		vkDestroyRenderPass(app.device, renderPass, nullptr);

		for (auto imageView : swapChainImageViews)
			vkDestroyImageView(app.device, imageView, nullptr);

		vkDestroySwapchainKHR(app.device, swapChain, nullptr);
	}

	void cleanup() {
		cleanupSwapChain();

		vkDestroySampler(app.device, textureSampler, nullptr);
		vkDestroyImageView(app.device, textureImageView, nullptr);
		vkDestroyImage(app.device, textureImage, nullptr);
		vkFreeMemory(app.device, textureImageMemory, nullptr);

		vkDestroyDescriptorPool(app.device, descriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(app.device, descriptorSetLayout, nullptr);

		vkDestroyBuffer(app.device, uniformBuffer, nullptr);
		vkFreeMemory(app.device, uniformBufferMemory, nullptr);
		vkDestroyBuffer(app.device, indexBuffer, nullptr);
		vkFreeMemory(app.device, indexBufferMemory, nullptr);
		vkDestroyBuffer(app.device, vertexBuffer, nullptr);
		vkFreeMemory(app.device, vertexBufferMemory, nullptr);

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

		createSwapChain();
		createImageViews();
		createRenderPass();
		createGraphicsPipeline();
		createDepthResources();
		createFramebuffers();
		createCommandBuffers();
	}

	void createSwapChain() {
		auto swapChainSupport = querySwapChainSupport(app.physicalDevice, app.surface);
		auto surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
		auto presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
		auto extent = chooseSwapExtent(app, swapChainSupport.capabilities);

		uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
		if (swapChainSupport.capabilities.maxImageCount > 0 &&
				swapChainSupport.capabilities.maxImageCount < imageCount) {
			imageCount = swapChainSupport.capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = app.surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		auto indices = findQueueFamilies(app.physicalDevice, app.surface);
		uint32_t queueFamilyIndices[] = {
			(uint32_t) indices.graphicsFamily,
			(uint32_t) indices.presentFamily
		};

		if (indices.graphicsFamily != indices.presentFamily) {
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		} else {
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}
		createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		VLKCHECK(vkCreateSwapchainKHR(app.device, &createInfo, nullptr, &swapChain));

		vkGetSwapchainImagesKHR(app.device, swapChain, &imageCount, nullptr);
		swapChainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(app.device, swapChain, &imageCount, swapChainImages.data());

		swapChainImageFormat = surfaceFormat.format;
		swapChainExtent = extent;
	}

	void createImageViews() {
		swapChainImageViews.resize(swapChainImages.size());
		for (size_t i = 0; i < swapChainImages.size(); ++i) {
			swapChainImageViews[i] = createImageView(
				swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
		}
	}

	void createRenderPass() {
		VkAttachmentDescription colorAttachment = {};
		colorAttachment.format = swapChainImageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentRef = {};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentDescription depthAttachment = {};
		depthAttachment.format = findDepthFormat(app.physicalDevice);
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachmentRef = {};
		depthAttachmentRef.attachment = 1;
		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		subpass.pDepthStencilAttachment = &depthAttachmentRef;

		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		const std::array<VkAttachmentDescription, 2> attachments = {
			colorAttachment, depthAttachment
		};

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = attachments.size();
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		VLKCHECK(vkCreateRenderPass(app.device, &renderPassInfo, nullptr, &renderPass));
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
		viewport.width = static_cast<float>(swapChainExtent.width);
		viewport.height = static_cast<float>(swapChainExtent.height);
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;

		VkRect2D scissor = {};
		scissor.offset = {0, 0};
		scissor.extent = swapChainExtent;

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
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
		pipelineInfo.basePipelineIndex = -1;

		VLKCHECK(vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline));

		// Cleanup
		vkDestroyShaderModule(app.device, fragShaderModule, nullptr);
		vkDestroyShaderModule(app.device, vertShaderModule, nullptr);
	}

	void createFramebuffers() {
		swapChainFramebuffers.resize(swapChainImageViews.size());
		for (size_t i = 0; i < swapChainImageViews.size(); ++i) {
			std::array<VkImageView, 2> attachments = {
				swapChainImageViews[i],
				depthImageView
			};

			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = attachments.size();
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = swapChainExtent.width;
			framebufferInfo.height = swapChainExtent.height;
			framebufferInfo.layers = 1;

			VLKCHECK(vkCreateFramebuffer(app.device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]));
		}
	}

	void createDepthResources() {
		auto depthFormat = findDepthFormat(app.physicalDevice);
		createImage(swapChainExtent.width, swapChainExtent.height, depthFormat,
				VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
		depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

		transitionImageLayout(depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}

	void createTextureImage() {
		int texWidth, texHeight, texChannels;
		auto pixels = stbi_load(cfg::TEXTURE_PATH, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		VkDeviceSize imageSize = texWidth * texHeight * 4;

		if (!pixels)
			throw std::runtime_error("failed to load texture image!");

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		createBuffer(app, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				stagingBuffer, stagingBufferMemory);

		void *data;
		vkMapMemory(app.device, stagingBufferMemory, 0, imageSize, 0, &data);
		memcpy(data, pixels, static_cast<size_t>(imageSize));
		vkUnmapMemory(app.device, stagingBufferMemory);

		stbi_image_free(pixels);

		createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);
		transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		copyBufferToImage(app, stagingBuffer, textureImage, texWidth, texHeight);
		transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		vkDestroyBuffer(app.device, stagingBuffer, nullptr);
		vkFreeMemory(app.device, stagingBufferMemory, nullptr);
	}

	void createTextureImageView() {
		textureImageView = createImageView(textureImage,
				VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
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

		VLKCHECK(vkCreateSampler(app.device, &samplerInfo, nullptr, &textureSampler));
	}

	VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const {
		VkImageViewCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = image;
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = format;;
		createInfo.subresourceRange.aspectMask = aspectFlags;
		createInfo.subresourceRange.baseMipLevel = 0;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.baseArrayLayer = 0;
		createInfo.subresourceRange.layerCount = 1;

		VkImageView imageView;
		VLKCHECK(vkCreateImageView(app.device, &createInfo, nullptr, &imageView));

		return imageView;
	}

	void createImage(
			uint32_t width,
			uint32_t height,
			VkFormat format,
			VkImageTiling tiling,
			VkImageUsageFlags usage,
			VkMemoryPropertyFlags properties,
			VkImage& image,
			VkDeviceMemory& imageMemory)
	{
		VkImageCreateInfo imageInfo = {};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = tiling;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = usage;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		VLKCHECK(vkCreateImage(app.device, &imageInfo, nullptr, &image));

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(app.device, image, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType(app, memRequirements.memoryTypeBits, properties);

		VLKCHECK(vkAllocateMemory(app.device, &allocInfo, nullptr, &imageMemory));

		vkBindImageMemory(app.device, image, imageMemory, 0);
	}

	void transitionImageLayout(VkImage image, VkFormat format,
			VkImageLayout oldLayout, VkImageLayout newLayout)
	{
		auto commandBuffer = beginSingleTimeCommands(app.device, app.commandPool);

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (hasStencilComponent(format))
				barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		} else {
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = 0;

		VkPipelineStageFlags sourceStage,
				     destinationStage;

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		} else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		} else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		} else {
			throw std::invalid_argument("unsupported layout transition!");
		}

		vkCmdPipelineBarrier(commandBuffer,
				sourceStage,
				destinationStage,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier);
		endSingleTimeCommands(app.device, app.queues.graphics, app.commandPool, commandBuffer);
	}

	/*
	void createVertexBuffer() {
		VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;

		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				stagingBuffer, stagingBufferMemory);

		void *data;
		vkMapMemory(app.device, stagingBufferMemory, 0, bufferSize, 0, &data);
		memcpy(data, vertices.data(), bufferSize);
		vkUnmapMemory(app.device, stagingBufferMemory);

		createBuffer(bufferSize,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);

		copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

		vkDestroyBuffer(app.device, stagingBuffer, nullptr);
		vkFreeMemory(app.device, stagingBufferMemory, nullptr);
	}
	*/

	void createVertexBuffer() {
		VkDeviceSize bufferSize = VERTEX_BUFFER_SIZE;

		createBuffer(app, bufferSize,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				vertexBuffer, vertexBufferMemory);
	}

	/*
	void createIndexBuffer() {
		VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				stagingBuffer, stagingBufferMemory);

		void *data;
		vkMapMemory(app.device, stagingBufferMemory, 0, bufferSize, 0, &data);
		memcpy(data, indices.data(), bufferSize);
		vkUnmapMemory(app.device, stagingBufferMemory);

		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				indexBuffer, indexBufferMemory);

		copyBuffer(stagingBuffer, indexBuffer, bufferSize);

		vkDestroyBuffer(app.device, stagingBuffer, nullptr);
		vkFreeMemory(app.device, stagingBufferMemory, nullptr);
	}
	*/

	void createIndexBuffer() {
		VkDeviceSize bufferSize = INDEX_BUFFER_SIZE;

		createBuffer(app, bufferSize,
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				indexBuffer, indexBufferMemory);
	}

	void createUniformBuffer() {
		VkDeviceSize bufferSize = sizeof(UniformBufferObject);
		createBuffer(app, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				uniformBuffer, uniformBufferMemory);
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
		bufferInfo.buffer = uniformBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(UniformBufferObject);

		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = textureImageView;
		imageInfo.sampler = textureSampler;

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
		commandBuffers.resize(swapChainFramebuffers.size());

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
			renderPassInfo.renderPass = renderPass;
			renderPassInfo.framebuffer = swapChainFramebuffers[i];
			renderPassInfo.renderArea.offset = {0, 0};
			renderPassInfo.renderArea.extent = swapChainExtent;
			std::array<VkClearValue, 2> clearValues = {};
			clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
			clearValues[1].depthStencil = {1.f, 0};
			renderPassInfo.clearValueCount = clearValues.size();
			renderPassInfo.pClearValues = clearValues.data();

			vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
			VkBuffer vertexBuffers[] = { vertexBuffer };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
			vkCmdDrawIndexed(commandBuffers[i], indices.size(), 1, 0, 0, 0);
			std::cerr << "recreating command buffer with v = " << vertices.size() << ", i = " << indices.size() << "\n";
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
		vkMapMemory(app.device, vertexBufferMemory, 0, VERTEX_BUFFER_SIZE, 0, &data);
		// Copy host memory to device
		memcpy(data, streamingBufferData, VERTEX_BUFFER_SIZE);
		vkUnmapMemory(app.device, vertexBufferMemory);
	}

	void updateIndexBuffer() {
		// Acquire handle to device memory
		void *data;
		vkMapMemory(app.device, indexBufferMemory, 0, INDEX_BUFFER_SIZE, 0, &data);
		// Copy host memory to device
		memcpy(data, (uint8_t*)streamingBufferData + VERTEX_BUFFER_SIZE, INDEX_BUFFER_SIZE);
		vkUnmapMemory(app.device, indexBufferMemory);
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
				swapChainExtent.width / float(swapChainExtent.height), 0.1f, 300.f);
		ubo.proj[1][1] *= -1;

		void *data;
		vkMapMemory(app.device, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
		memcpy(data, &ubo, sizeof(ubo));
		vkUnmapMemory(app.device, uniformBufferMemory);
	}


	void drawFrame() {

		uint32_t imageIndex;
		auto result = vkAcquireNextImageKHR(app.device, swapChain, std::numeric_limits<uint64_t>::max(),
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
		VkSwapchainKHR swapChains[] = { swapChain };
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
	HelloTriangleApplication app;

	try {
		app.run();
	} catch (const std::runtime_error& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
