#include "vulk_memory.hpp"
#include "vulk_errors.hpp"
#include <cassert>
#include <stdexcept>

#ifndef NDEBUG
MemoryMonitor gMemMonitor;
#endif

MemoryChunk createMemoryChunk(VkDevice device, VkDeviceSize size, uint32_t memoryTypeIndex) {
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.memoryTypeIndex = memoryTypeIndex;

	MemoryChunk chunk;

	MemoryBlock block;
	block.offset = 0;
	block.size = size;
	block.memType = memoryTypeIndex;
	VLKCHECK(vkAllocateMemory(device, &allocInfo, nullptr, &block.memory));

	chunk.memory = block.memory;
	chunk.blocks.emplace_back(block);

	return chunk;
}

bool MemoryChunk::alloc(VkDeviceSize size, VkDeviceSize align, MemoryBlock& outBlock) {
	if (this->size < size)
		return false;

	for (unsigned i = 0; i < blocks.size(); ++i) {
		auto& block = blocks[i];
		if (block._idx >= 0) continue;

		auto newSize = block.size;
		// Take alignment requirements into account
		const auto alignOff = block.offset % align;
		if (alignOff != 0)
			newSize -= align - alignOff;

		if (newSize < size) continue;

		block.size = newSize;
		// Realign if needed
		if (alignOff != 0) {
			const auto alignSkip = align - alignOff;
			block.offset += alignSkip;
			// Merge skipped space into previous block to
			// prevent further fragmentation.
			// Note: no need to check i > 0: that's obvious.
			blocks[i - 1].size += alignSkip;
		}

		if (block.size != size) {
			// Not a perfect fit: create a new block after this one
			MemoryBlock nextBlock;
			nextBlock.offset = block.offset + size;
			nextBlock.memory = memory;
			nextBlock.memType = memoryTypeIndex;
			nextBlock.size = block.size - size;
			blocks.emplace_back(nextBlock);
		}

		block.size = size;
		block._idx = blockIdx++;
		outBlock = block;

		return true;
	}

	return false;
}

void MemoryChunk::dealloc(MemoryBlock& block) {

	assert(block.memType == memoryTypeIndex && "Block does not belong to this chunk!");

	bool mergedBefore = false,
	     mergedAfter = false;
	uint32_t idx = -1;
	for (uint32_t i = 0; i < blocks.size(); ++i) {
		if (blocks[i]._idx != block._idx)
			continue;

		idx = i;
		blocks[i]._idx = -1;

		// If adjacent blocks are free, merge them into one block.
		if (i < blocks.size() - 1 && blocks[i + 1]._idx < 0) {
			mergedAfter = true;
			blocks[i].size += blocks[i + 1].size;
		}
		if (i > 0 && blocks[i - 1]._idx < 0) {
			mergedBefore = true;
			blocks[i - 1].size += block.size;
		}
	}

	assert(idx >= 0 && "Deallocating inexisting block!");

	if (mergedAfter)
		blocks.erase(blocks.begin() + idx + 1);

	if (mergedBefore)
		blocks.erase(blocks.begin() + idx);
}

MemoryBlock VulkanAllocator::alloc(uint32_t type, VkDeviceSize size, VkDeviceSize align) {
	constexpr auto CHUNK_SIZE = 1 << 16;
	if (chunks.count(type) == 0)
		chunks[type] = createMemoryChunk(device, CHUNK_SIZE, type);

	MemoryBlock block;
	if (!chunks[type].alloc(size, align, block))
		throw std::runtime_error("Failed to allocate memory block!");


	return block;
}

void VulkanAllocator::dealloc(MemoryBlock& block) {
	chunks[block.memType].dealloc(block);
}
