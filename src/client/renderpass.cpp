#include "renderpass.hpp"
#include "application.hpp"
#include "vulk_errors.hpp"
#include "formats.hpp"
#include <array>

VkRenderPass createGeometryRenderPass(const Application& app) {

	std::vector<VkAttachmentDescription> colorAttachDesc{ 4 };

	const std::array<Image, 3> attachments = {{
		app.gBuffer.position,
		app.gBuffer.normal,
		app.gBuffer.albedoSpec
	}};

	// 1- world space position
	// 2- world space normal
	// 3- albedo + specular
	// 4- depth
	constexpr auto depthIdx = 3;
	for (unsigned i = 0; i < 4; ++i) {
		if (i < attachments.size()) {
			colorAttachDesc[i].format = attachments[i].format;
			colorAttachDesc[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		} else {
			colorAttachDesc[i].format = formats::depth;
			colorAttachDesc[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
		colorAttachDesc[i].samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachDesc[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachDesc[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachDesc[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachDesc[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachDesc[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	const std::array<VkAttachmentReference, 3> colorAttachRefs = {
		VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
		VkAttachmentReference{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
		VkAttachmentReference{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
	};

	VkAttachmentReference depthAttachRef = {};
	depthAttachRef.attachment = depthIdx;
	depthAttachRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = colorAttachRefs.size();
	subpass.pColorAttachments = colorAttachRefs.data();
	subpass.pDepthStencilAttachment = &depthAttachRef;

	std::array<VkSubpassDependency, 2> deps;

	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = colorAttachDesc.size();
	renderPassInfo.pAttachments = colorAttachDesc.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = deps.size();
	renderPassInfo.pDependencies = deps.data();

	VkRenderPass renderPassHandle;
	VLKCHECK(vkCreateRenderPass(app.device, &renderPassInfo, nullptr, &renderPassHandle));
	app.validation.addObjectInfo(renderPassHandle, __FILE__, __LINE__);

	return renderPassHandle;
}

VkRenderPass createLightingRenderPass(const Application& app) {
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = app.swapChain.imageFormat;
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
	depthAttachment.format = formats::depth;
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

	VkRenderPass renderPass;
	VLKCHECK(vkCreateRenderPass(app.device, &renderPassInfo, nullptr, &renderPass));
	app.validation.addObjectInfo(renderPass, __FILE__, __LINE__);

	return renderPass;
}

VkRenderPass createRenderPass(const Application& app, const std::vector<Image>& attachments,
		const Image& diffuse, const Image& specular) {

	std::vector<VkAttachmentDescription> attachDesc{ 4 + attachments.size() };

	// 0- diffuse (in / -)
	// 1- specular (in / -)
	// 2- world space position (out / in)
	// 3- world space normal (out / in)
	// 4- albedo + specular (out / in)
	// 5- color output (- / out)
	// 6- depth output (- / out)
	for (unsigned i = 0; i < attachDesc.size(); ++i) {
		attachDesc[i].format = attachments[i].format;
		attachDesc[i].samples = VK_SAMPLE_COUNT_1_BIT;
		attachDesc[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachDesc[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachDesc[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachDesc[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachDesc[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachDesc[i].finalLayout = (i == 6)
			? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
			: VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	const std::array<VkAttachmentReference, 2> geomInputAttachments = {
		VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
		VkAttachmentReference{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
	};

	const std::array<VkAttachmentReference, 3> geomColorAttachments = {
		VkAttachmentReference{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
		VkAttachmentReference{ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
		VkAttachmentReference{ 4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
	};

	const std::array<VkAttachmentReference, 3> lightInputAttachments = {
		VkAttachmentReference{ 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		VkAttachmentReference{ 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		VkAttachmentReference{ 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
	};

	VkAttachmentDescription lightColorAttachment = {};
	lightColorAttachment.format = app.swapChain.imageFormat;
	lightColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	lightColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	lightColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	lightColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	lightColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	lightColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	lightColorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference lightColorAttachmentRef = {};
	lightColorAttachmentRef.attachment = 5;
	lightColorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachRef = {};
	depthAttachRef.attachment = 6;
	depthAttachRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription geomSubpass = {};
	geomSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	geomSubpass.inputAttachmentCount = geomInputAttachments.size();
	geomSubpass.pInputAttachments = geomInputAttachments.data();
	geomSubpass.colorAttachmentCount = geomColorAttachments.size();
	geomSubpass.pColorAttachments = geomColorAttachments.data();

	VkSubpassDescription lightSubpass = {};
	lightSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	lightSubpass.inputAttachmentCount = lightInputAttachments.size();
	lightSubpass.pInputAttachments = lightInputAttachments.data();
	lightSubpass.colorAttachmentCount = 1;
	lightSubpass.pColorAttachments = &lightColorAttachmentRef;
	lightSubpass.pDepthStencilAttachment = &depthAttachRef;

	const std::array<VkSubpassDescription, 2> subpasses = {
		geomSubpass,
		lightSubpass
	};

	std::array<VkSubpassDependency, 3> deps;

	// external -> geom
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// geom -> light
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = 1;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// light -> external
	deps[2].srcSubpass = 1;
	deps[2].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	deps[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	deps[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = attachDesc.size();
	renderPassInfo.pAttachments = attachDesc.data();
	renderPassInfo.subpassCount = subpasses.size();
	renderPassInfo.pSubpasses = subpasses.data();
	renderPassInfo.dependencyCount = deps.size();
	renderPassInfo.pDependencies = deps.data();

	VkRenderPass renderPassHandle;
	VLKCHECK(vkCreateRenderPass(app.device, &renderPassInfo, nullptr, &renderPassHandle));
	app.validation.addObjectInfo(renderPassHandle, __FILE__, __LINE__);

	return renderPassHandle;
}
