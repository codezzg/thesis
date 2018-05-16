#include "commands.hpp"
#include "phys_device.hpp"
#include "vulk_errors.hpp"
#include "application.hpp"
#include <stdexcept>

VkCommandPool createCommandPool(const Application& app) {
	auto queueFamilyIndices = findQueueFamilies(app.physicalDevice, app.surface);

	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

	VkCommandPool commandPool;
	VLKCHECK(vkCreateCommandPool(app.device, &poolInfo, nullptr, &commandPool));
	app.validation.addObjectInfo(commandPool, __FILE__, __LINE__);

	return commandPool;
}

VkCommandBuffer allocCommandBuffer(const Application& app, VkCommandPool commandPool) {
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	VLKCHECK(vkAllocateCommandBuffers(app.device, &allocInfo, &commandBuffer));
	app.validation.addObjectInfo(commandBuffer, __FILE__, __LINE__);

	return commandBuffer;
}

VkCommandBuffer beginSingleTimeCommands(const Application& app, VkCommandPool commandPool) {
	auto commandBuffer = allocCommandBuffer(app, commandPool);
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VLKCHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

	return commandBuffer;
}

void endSingleTimeCommands(VkDevice device, VkQueue graphicsQueue,
		VkCommandPool commandPool, VkCommandBuffer commandBuffer)
{
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	VLKCHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
	VLKCHECK(vkQueueWaitIdle(graphicsQueue));

	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

