#include "pipelines.hpp"
#include "application.hpp"
#include "formats.hpp"
#include "logging.hpp"
#include "shaders.hpp"
#include "shared_resources.hpp"
#include "to_string.hpp"
#include "vulk_errors.hpp"
#include <algorithm>

using namespace logging;

VkPipelineLayout createPipelineLayout(const Application& app,
	const std::vector<VkDescriptorSetLayout>& descSetLayouts,
	const std::vector<VkPushConstantRange>& pushConstantRanges)
{
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = descSetLayouts.size(),
	pipelineLayoutInfo.pSetLayouts = descSetLayouts.data(),
	pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();

	VkPipelineLayout pipelineLayout;
	VLKCHECK(vkCreatePipelineLayout(app.device, &pipelineLayoutInfo, nullptr, &pipelineLayout));
	app.validation.addObjectInfo(pipelineLayout, __FILE__, __LINE__);

	return pipelineLayout;
}

VkPipelineCache createPipelineCache(const Application& app)
{
	VkPipelineCacheCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

	VkPipelineCache pipelineCache;
	VLKCHECK(vkCreatePipelineCache(app.device, &createInfo, nullptr, &pipelineCache));
	app.validation.addObjectInfo(pipelineCache, __FILE__, __LINE__);

	return pipelineCache;
}

std::vector<VkPipeline> createPipelines(const Application& app, const std::vector<shared::SpirvShader>& shaders)
{
	using shared::ShaderStage;

	///////// Begin common stuff
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport = {};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = static_cast<float>(app.swapChain.extent.width);     // GBUF_DIM);
	viewport.height = static_cast<float>(app.swapChain.extent.height);   // GBUF_DIM);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = app.swapChain.extent;   // { GBUF_DIM, GBUF_DIM };

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

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
	colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
						   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachmentState.blendEnable = VK_FALSE;
	colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

	const std::array<VkPipelineColorBlendAttachmentState, 3> colorBlendAttachmentStates = {
		colorBlendAttachmentState,
		colorBlendAttachmentState,
		colorBlendAttachmentState,
	};

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = colorBlendAttachmentStates.size();
	colorBlending.pAttachments = colorBlendAttachmentStates.data();

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.stencilTestEnable = VK_FALSE;

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	const auto bindingDescription = getVertexBindingDescription();
	const auto attributeDescriptions = getVertexAttributeDescriptions();
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size();
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {};

	//////////// End common stuff

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = shaderStages.size();
	pipelineInfo.pStages = shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pDynamicState = nullptr;
	pipelineInfo.layout = app.res.pipelineLayouts->get("multi");
	pipelineInfo.renderPass = app.renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

	std::vector<VkPipeline> pipelines;

	const auto mustFindShader = [&shaders](uint8_t passNumber, ShaderStage stage) -> shared::SpirvShader {
		auto shad_it = std::find_if(shaders.begin(), shaders.end(), [&](const auto& sh) {
			return sh.passNumber == passNumber && sh.stage == stage;
		});
		if (shad_it == shaders.end()) {
			err("Couldn't find shader for pass ", passNumber, " and stage ", stage, "!");
			throw;
		}
		return *shad_it;
	};

	/// Multipass
	{
		auto vertShaderModule = createShaderModule(app, mustFindShader(0, ShaderStage::VERTEX));
		auto fragShaderModule = createShaderModule(app, mustFindShader(0, ShaderStage::FRAGMENT));

		VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName = "main";
		shaderStages[0] = vertShaderStageInfo;

		VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main";
		shaderStages[1] = fragShaderStageInfo;

		VkPipeline pipeline;
		VLKCHECK(
			vkCreateGraphicsPipelines(app.device, app.pipelineCache, 1, &pipelineInfo, nullptr, &pipeline));
		app.validation.addObjectInfo(pipeline, __FILE__, __LINE__);
		pipelines.emplace_back(pipeline);

		// Cleanup
		vkDestroyShaderModule(app.device, fragShaderModule, nullptr);
		vkDestroyShaderModule(app.device, vertShaderModule, nullptr);
	}

	/// Skybox
	{
		colorBlending.attachmentCount = 1;
		pipelineInfo.subpass = 1;

		auto vertShaderModule = createShaderModule(app, mustFindShader(1, ShaderStage::VERTEX));
		auto fragShaderModule = createShaderModule(app, mustFindShader(1, ShaderStage::FRAGMENT));

		VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName = "main";
		shaderStages[0] = vertShaderStageInfo;

		VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main";
		shaderStages[1] = fragShaderStageInfo;

		VkPipeline pipeline;
		VLKCHECK(
			vkCreateGraphicsPipelines(app.device, app.pipelineCache, 1, &pipelineInfo, nullptr, &pipeline));
		app.validation.addObjectInfo(pipeline, __FILE__, __LINE__);
		pipelines.emplace_back(pipeline);

		// Cleanup
		vkDestroyShaderModule(app.device, fragShaderModule, nullptr);
		vkDestroyShaderModule(app.device, vertShaderModule, nullptr);
	}

	/// Swap
	{
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		rasterizer.cullMode = VK_CULL_MODE_NONE;
		colorBlending.attachmentCount = 1;
		pipelineInfo.subpass = 2;

		auto vertShaderModule = createShaderModule(app, mustFindShader(2, ShaderStage::VERTEX));
		auto fragShaderModule = createShaderModule(app, mustFindShader(2, ShaderStage::FRAGMENT));

		VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName = "main";
		shaderStages[0] = vertShaderStageInfo;

		VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main";
		shaderStages[1] = fragShaderStageInfo;

		VkPipeline pipeline;
		VLKCHECK(
			vkCreateGraphicsPipelines(app.device, app.pipelineCache, 1, &pipelineInfo, nullptr, &pipeline));
		app.validation.addObjectInfo(pipeline, __FILE__, __LINE__);
		pipelines.emplace_back(pipeline);

		// Cleanup
		vkDestroyShaderModule(app.device, fragShaderModule, nullptr);
		vkDestroyShaderModule(app.device, vertShaderModule, nullptr);
	}

	return pipelines;
}

