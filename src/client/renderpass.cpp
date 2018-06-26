#include "renderpass.hpp"
#include "application.hpp"
#include "formats.hpp"
#include "vulk_errors.hpp"
#include <array>

VkRenderPass createMultipassRenderPass(const Application& app)
{
	std::array<VkAttachmentDescription, 5> attachDesc = {};

	// Color backbuffer
	attachDesc[0].format = app.swapChain.imageFormat;
	attachDesc[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachDesc[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachDesc[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachDesc[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachDesc[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachDesc[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachDesc[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	// Depth
	attachDesc[1] = attachDesc[0];
	attachDesc[1].format = formats::depth;
	attachDesc[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachDesc[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachDesc[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachDesc[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

	/// Input attachments
	// position
	attachDesc[2] = attachDesc[0];
	attachDesc[2].format = formats::position;
	attachDesc[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachDesc[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// normal
	attachDesc[3] = attachDesc[2];
	attachDesc[3].format = formats::normal;

	// albedo/spec
	attachDesc[4] = attachDesc[3];
	attachDesc[4].format = formats::albedoSpec;

	const std::array<VkAttachmentReference, 3> colorRefs = {
		VkAttachmentReference{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
		VkAttachmentReference{ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
		VkAttachmentReference{ 4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
	};
	VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	const std::array<uint32_t, 1> presRefs = {
		3   // normal
	};

	const std::array<VkAttachmentReference, 4> inputRefs = {
		VkAttachmentReference{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
		VkAttachmentReference{ 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		VkAttachmentReference{ 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		VkAttachmentReference{ 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
	};
	VkAttachmentReference depthRORef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

	VkAttachmentReference outColorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

	VkSubpassDescription geomSubpass = {};
	geomSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	geomSubpass.colorAttachmentCount = colorRefs.size();
	geomSubpass.pColorAttachments = colorRefs.data();
	geomSubpass.pDepthStencilAttachment = &depthRef;

	VkSubpassDescription skySubpass = {};
	skySubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	// We write to albedo
	skySubpass.colorAttachmentCount = 1;
	skySubpass.pColorAttachments = &colorRefs[2];
	// We preserve normal
	skySubpass.preserveAttachmentCount = presRefs.size();
	skySubpass.pPreserveAttachments = presRefs.data();
	// We read position
	skySubpass.inputAttachmentCount = 1;
	skySubpass.pInputAttachments = &inputRefs[1];

	VkSubpassDescription lightSubpass = {};
	lightSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	lightSubpass.inputAttachmentCount = inputRefs.size();
	lightSubpass.pInputAttachments = inputRefs.data();
	lightSubpass.colorAttachmentCount = 1;
	lightSubpass.pColorAttachments = &outColorRef;
	lightSubpass.pDepthStencilAttachment = &depthRORef;

	const std::array<VkSubpassDescription, 3> subpasses = { geomSubpass, skySubpass, lightSubpass };

	std::array<VkSubpassDependency, 3> subpassDependencies;

	subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependencies[0].dstSubpass = 0;
	subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
					      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
					      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	subpassDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
					      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
					      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	subpassDependencies[0].srcAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	subpassDependencies[0].dstAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	subpassDependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	subpassDependencies[1].srcSubpass = 0;
	subpassDependencies[1].dstSubpass = 1;
	subpassDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
					      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
					      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	subpassDependencies[1].dstStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	subpassDependencies[1].srcAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	subpassDependencies[1].dstAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	subpassDependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	subpassDependencies[2].srcSubpass = 1;
	subpassDependencies[2].dstSubpass = 2;
	subpassDependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
					      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
					      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	subpassDependencies[2].dstStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	subpassDependencies[2].srcAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	subpassDependencies[2].dstAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	subpassDependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = attachDesc.size();
	renderPassInfo.pAttachments = attachDesc.data();
	renderPassInfo.subpassCount = subpasses.size();
	renderPassInfo.pSubpasses = subpasses.data();
	renderPassInfo.dependencyCount = subpassDependencies.size();
	renderPassInfo.pDependencies = subpassDependencies.data();

	VkRenderPass renderPassHandle;
	VLKCHECK(vkCreateRenderPass(app.device, &renderPassInfo, nullptr, &renderPassHandle));
	app.validation.addObjectInfo(renderPassHandle, __FILE__, __LINE__);

	return renderPassHandle;
}
