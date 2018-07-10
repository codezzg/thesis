#include "shaders.hpp"
#include "application.hpp"
#include "shared_resources.hpp"
#include "utils.hpp"
#include "vulk_errors.hpp"
#include <vector>

VkShaderModule createShaderModule(const Application& app, const char* fname)
{
	const auto code = readFile(fname);

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shaderModule;
	VLKCHECK(vkCreateShaderModule(app.device, &createInfo, nullptr, &shaderModule));
	app.validation.addObjectInfo(shaderModule, __FILE__, __LINE__);

	return shaderModule;
}

VkShaderModule createShaderModule(const Application& app, const shared::SpirvShader& shader)
{
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = shader.codeSizeInBytes;
	createInfo.pCode = shader.code;

	VkShaderModule shaderModule;
	VLKCHECK(vkCreateShaderModule(app.device, &createInfo, nullptr, &shaderModule));
	app.validation.addObjectInfo(shaderModule, __FILE__, __LINE__);

	return shaderModule;
}

std::string shaderStageToExt(shared::ShaderStage s)
{
	switch (s) {
		using S = shared::ShaderStage;
	case S::VERTEX:
		return ".vert";
	case S::FRAGMENT:
		return ".frag";
	case S::GEOMETRY:
		return ".geom";
	default:
		return "???";
	}
}
