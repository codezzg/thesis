#include "images.hpp"
#include "application.hpp"
#include "commands.hpp"
#include "formats.hpp"
#include "logging.hpp"
#include "phys_device.hpp"
#include "vulk_errors.hpp"
#include "vulk_memory.hpp"
#include <unordered_set>

using namespace logging;

void ImageAllocator::addImage(Image& image,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties)
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

	createInfos.emplace_back(imageInfo);
	this->properties.emplace_back(properties);

	image.format = format;
	images.emplace_back(&image);
}

void ImageAllocator::create(const Application& app)
{
	// (memory type) => (memory size)
	std::unordered_map<uint32_t, VkDeviceSize> requiredSizes;

	std::vector<uint32_t> memTypesNeeded;
	memTypesNeeded.reserve(createInfos.size());

	// Create the images and figure out what memory they need
	for (unsigned i = 0; i < createInfos.size(); ++i) {
		VkImage imageHandle;
		VLKCHECK(vkCreateImage(app.device, &createInfos[i], nullptr, &imageHandle));
		app.validation.addObjectInfo(imageHandle, __FILE__, __LINE__);
		images[i]->handle = imageHandle;

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(app.device, imageHandle, &memRequirements);

		const auto memType = findMemoryType(app.physicalDevice, memRequirements.memoryTypeBits, properties[i]);
		images[i]->offset = requiredSizes[memType];
		requiredSizes[memType] += memRequirements.size;

		memTypesNeeded.emplace_back(memType);
	}

	// The newly allocated device memories
	std::unordered_map<uint32_t, VkDeviceMemory> memories;
	memories.reserve(requiredSizes.size());

	// Allocate memory of the proper types
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	for (const auto& pair : requiredSizes) {
		allocInfo.memoryTypeIndex = pair.first;
		allocInfo.allocationSize = pair.second;
		VkDeviceMemory imageMemory;
		VLKCHECK(vkAllocateMemory(app.device, &allocInfo, nullptr, &imageMemory));
		app.validation.addObjectInfo(imageMemory, __FILE__, __LINE__);
#ifndef NDEBUG
		gMemMonitor.newAlloc(imageMemory, allocInfo);
#endif

		memories[pair.first] = imageMemory;
	}

	// Bind the memory to the images
	for (unsigned i = 0; i < images.size(); ++i) {
		auto& memType = memTypesNeeded[i];
		auto img = images[i];
		VLKCHECK(vkBindImageMemory(app.device, img->handle, memories[memType], img->offset));
		img->memory = memories[memType];
	}

	info("Created ", images.size(), " images via ", memories.size(), " allocations.");
}

Image createImage(const Application& app,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties)
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

	VkImage imageHandle;
	VLKCHECK(vkCreateImage(app.device, &imageInfo, nullptr, &imageHandle));
	app.validation.addObjectInfo(imageHandle, __FILE__, __LINE__);

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(app.device, imageHandle, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(app.physicalDevice, memRequirements.memoryTypeBits, properties);

	VkDeviceMemory imageMemory;
	VLKCHECK(vkAllocateMemory(app.device, &allocInfo, nullptr, &imageMemory));
	app.validation.addObjectInfo(imageMemory, __FILE__, __LINE__);
#ifndef NDEBUG
	gMemMonitor.newAlloc(imageMemory, allocInfo);
#endif

	VLKCHECK(vkBindImageMemory(app.device, imageHandle, imageMemory, 0));

	Image image;
	image.handle = imageHandle;
	image.memory = imageMemory;
	image.format = format;

	return image;
}

VkImageView createImageView(const Application& app, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{
	VkImageViewCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	createInfo.image = image;
	createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	createInfo.format = format;
	createInfo.subresourceRange.aspectMask = aspectFlags;
	createInfo.subresourceRange.baseMipLevel = 0;
	createInfo.subresourceRange.levelCount = 1;
	createInfo.subresourceRange.baseArrayLayer = 0;
	createInfo.subresourceRange.layerCount = 1;

	VkImageView imageView;
	VLKCHECK(vkCreateImageView(app.device, &createInfo, nullptr, &imageView));
	app.validation.addObjectInfo(imageView, __FILE__, __LINE__);

	return imageView;
}

void transitionImageLayout(const Application& app,
        VkImage image,
        VkFormat format,
        VkImageLayout oldLayout,
        VkImageLayout newLayout)
{
	auto commandBuffer = beginSingleTimeCommands(app, app.commandPool);

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

	VkPipelineStageFlags sourceStage, destinationStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
	           newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	} else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
	           newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask =
		        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	} else {
		throw std::invalid_argument("unsupported layout transition!");
	}

	vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	endSingleTimeCommands(app.device, app.queues.graphics, app.commandPool, commandBuffer);
}

Image createDepthImage(const Application& app)
{
	auto depthImage = createImage(app,
	        app.swapChain.extent.width,
	        app.swapChain.extent.height,
	        formats::depth,
	        VK_IMAGE_TILING_OPTIMAL,
	        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
	                VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
	        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	depthImage.view = createImageView(app, depthImage.handle, formats::depth, VK_IMAGE_ASPECT_DEPTH_BIT);

	transitionImageLayout(app,
	        depthImage.handle,
	        formats::depth,
	        VK_IMAGE_LAYOUT_UNDEFINED,
	        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	return depthImage;
}

void destroyImage(VkDevice device, Image image)
{
	if (image.view != VK_NULL_HANDLE)
		vkDestroyImageView(device, image.view, nullptr);
	vkDestroyImage(device, image.handle, nullptr);
	vkFreeMemory(device, image.memory, nullptr);
#ifndef NDEBUG
	gMemMonitor.newFree(image.memory);
#endif
}

void destroyAllImages(VkDevice device, const std::vector<Image>& images)
{
	std::unordered_set<VkDeviceMemory> mems;
	for (auto& i : images) {
		mems.emplace(i.memory);
		if (i.view != VK_NULL_HANDLE)
			vkDestroyImageView(device, i.view, nullptr);
		vkDestroyImage(device, i.handle, nullptr);
	}

	for (auto& mem : mems) {
		vkFreeMemory(device, mem, nullptr);
#ifndef NDEBUG
		gMemMonitor.newFree(mem);
#endif
	}
}
