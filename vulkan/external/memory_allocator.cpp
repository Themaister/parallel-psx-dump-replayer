/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2016 ARM Limited
 *     ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "memory_allocator.hpp"
#include <algorithm>
#include <stdio.h>
#include <string.h>

using namespace std;

namespace MaliSDK
{

#ifdef VULKAN_TEST
#define MEMORY_ASSERT(x) \
	do                   \
	{                    \
		if (!(x))        \
			throw #x;    \
	} while (0)
#else
#define MEMORY_ASSERT(x) ((void)0)
#endif

#ifdef VULKAN_TEST
static uint32_t globalCookie = 0;
#endif

void DeviceAllocation::freeImmediate()
{
	if (!pAlloc)
		return;

#ifdef VULKAN_TEST
	if (cookie && pHostBase)
	{
		auto *pPtr = reinterpret_cast<uint32_t *>(pHostBase);
		for (unsigned i = 0; i < (size >> 2); i++)
			MEMORY_ASSERT(pPtr[i] == cookie);
	}
#endif

	pAlloc->free(this);
	pAlloc = nullptr;
	base = {};
	mask = 0;
	offset = 0;
}

void DeviceAllocation::freeGlobal(GlobalAllocator &allocator, uint32_t size, uint32_t memoryType)
{
	if (base)
	{
#ifdef VULKAN_TEST
		if (cookie && pHostBase)
		{
			auto *pPtr = reinterpret_cast<uint32_t *>(pHostBase);
			for (unsigned i = 0; i < (size >> 2); i++)
				MEMORY_ASSERT(pPtr[i] == cookie);
		}
#endif

		allocator.free(size, memoryType, base, pHostBase);
		base = {};
		mask = 0;
		offset = 0;
	}
}

#ifdef VULKAN_TEST
void DeviceAllocation::initCookie()
{
	if (pHostBase)
	{
		uint32_t *ptr = reinterpret_cast<uint32_t *>(pHostBase);
		cookie = ++globalCookie;
		for (unsigned i = 0; i < (size >> 2); i++)
			ptr[i] = cookie;
	}
}
#endif

void Block::allocate(uint32_t numBlocks, DeviceAllocation *pBlock)
{
	MEMORY_ASSERT(NumSubBlocks >= numBlocks);
	MEMORY_ASSERT(numBlocks != 0);

	uint32_t blockMask;
	if (numBlocks == NumSubBlocks)
		blockMask = ~0u;
	else
		blockMask = ((1u << numBlocks) - 1u);

	uint32_t mask = freeBlocks[numBlocks - 1];
	uint32_t b = ctz(mask);

	MEMORY_ASSERT(((freeBlocks[0] >> b) & blockMask) == blockMask);

	uint32_t sb = blockMask << b;
	freeBlocks[0] &= ~sb;
	updateLongestRun();

	pBlock->mask = sb;
	pBlock->offset = b;
}

void Block::free(uint32_t mask)
{
	MEMORY_ASSERT((freeBlocks[0] & mask) == 0);
	freeBlocks[0] |= mask;
	updateLongestRun();
}

void ClassAllocator::suballocate(uint32_t numBlocks, uint32_t tiling, uint32_t memoryType, MiniHeap &heap,
                                 DeviceAllocation *pAlloc)
{
	heap.heap.allocate(numBlocks, pAlloc);
	pAlloc->base = heap.allocation.base;
	pAlloc->offset <<= subBlockSizeLog2;

	if (heap.allocation.pHostBase)
		pAlloc->pHostBase = heap.allocation.pHostBase + pAlloc->offset;

	pAlloc->offset += heap.allocation.offset;
	pAlloc->tiling = tiling;
	pAlloc->memoryType = memoryType;
	pAlloc->pAlloc = this;
	pAlloc->size = numBlocks << subBlockSizeLog2;
}

bool ClassAllocator::allocate(uint32_t size, AllocationTiling tiling, DeviceAllocation *pAlloc, bool hierarchical)
{
	unsigned numBlocks = (size + subBlockSize - 1) >> subBlockSizeLog2;
	uint32_t sizeMask = (1u << (numBlocks - 1)) - 1;
	uint32_t maskedTilingMode = tilingMask & tiling;
	auto &m = tilingModes[maskedTilingMode];

#ifdef MEMORY_ALLOCATOR_THREAD_SAFE
	lock_guard<mutex> holder{ lock };
#endif

	uint32_t index = ctz(m.heapAvailabilityMask & ~sizeMask);

	if (index < Block::NumSubBlocks)
	{
		auto itr = m.heaps[index].begin();
		MEMORY_ASSERT(itr);
		MEMORY_ASSERT(index >= (numBlocks - 1));

		auto &heap = itr->storage;
		suballocate(numBlocks, maskedTilingMode, memoryType, heap, pAlloc);
		unsigned newIndex = heap.heap.getLongestRun() - 1;

		if (heap.heap.full())
		{
			m.fullHeaps.moveToFront(m.heaps[index], itr);
			if (!m.heaps[index].begin())
				m.heapAvailabilityMask &= ~(1u << index);
		}
		else if (newIndex != index)
		{
			auto &newHeap = m.heaps[newIndex];
			newHeap.moveToFront(m.heaps[index], itr);
			m.heapAvailabilityMask |= 1u << newIndex;
			if (!m.heaps[index].begin())
				m.heapAvailabilityMask &= ~(1u << index);
		}

		pAlloc->pHeap = itr;
		pAlloc->hierarchical = hierarchical;

#ifdef VULKAN_TEST
		if (hierarchical)
			hierOccupiedSize += popcount(pAlloc->mask) * subBlockSize;
		else
			occupiedSize += popcount(pAlloc->mask) * subBlockSize;
#endif
		return true;
	}

	// We didn't find a vacant heap, make a new one.
	auto *pNode = objectPool.allocate();
	if (!pNode)
		return false;

	auto &heap = pNode->storage;
	uint32_t allocSize = subBlockSize * Block::NumSubBlocks;

	if (pParent)
	{
		// We cannot allocate a new block from parent ... This is fatal.
		if (!pParent->allocate(allocSize, tiling, &heap.allocation, true))
		{
			objectPool.free(pNode);
			return false;
		}
	}
	else
	{
		heap.allocation.offset = 0;
		if (!pGlobalAllocator->allocate(allocSize, memoryType, &heap.allocation.base, &heap.allocation.pHostBase))
		{
			objectPool.free(pNode);
			return false;
		}
	}

	// This cannot fail.
	suballocate(numBlocks, maskedTilingMode, memoryType, heap, pAlloc);

	pAlloc->pHeap = pNode;
	if (heap.heap.full())
	{
		m.fullHeaps.insertFront(pNode);
	}
	else
	{
		unsigned newIndex = heap.heap.getLongestRun() - 1;
		m.heaps[newIndex].insertFront(pNode);
		m.heapAvailabilityMask |= 1u << newIndex;
	}

	pAlloc->hierarchical = hierarchical;

#ifdef VULKAN_TEST
	if (hierarchical)
		hierOccupiedSize += popcount(pAlloc->mask) * subBlockSize;
	else
		occupiedSize += popcount(pAlloc->mask) * subBlockSize;
#endif

	return true;
}

ClassAllocator::~ClassAllocator()
{
	bool error = false;
	for (auto &m : tilingModes)
	{
		if (m.fullHeaps.begin())
			error = true;

		for (auto &h : m.heaps)
			if (h.begin())
				error = true;
	}

	if (error)
		LOG("Memory leaked in class allocator!\n");
}

void ClassAllocator::free(DeviceAllocation *pAlloc)
{
	auto *pHeap = pAlloc->pHeap;
	auto &block = pHeap->storage.heap;
	bool wasFull = block.full();
	auto &m = tilingModes[pAlloc->tiling];

#ifdef MEMORY_ALLOCATOR_THREAD_SAFE
	lock_guard<mutex> holder{ lock };
#endif

#ifdef VULKAN_TEST
	uint32_t used = popcount(pAlloc->mask) * subBlockSize;
	if (pAlloc->hierarchical)
	{
		MEMORY_ASSERT(used <= hierOccupiedSize);
		hierOccupiedSize -= used;
	}
	else
	{
		MEMORY_ASSERT(used <= occupiedSize);
		occupiedSize -= used;
	}
#endif

	unsigned index = block.getLongestRun() - 1;
	block.free(pAlloc->mask);
	unsigned newIndex = block.getLongestRun() - 1;

	if (block.empty())
	{
		// Our mini-heap is completely freed, free to higher level allocator.
		if (pParent)
			pHeap->storage.allocation.freeImmediate();
		else
			pHeap->storage.allocation.freeGlobal(*pGlobalAllocator, subBlockSize * Block::NumSubBlocks, memoryType);

		if (wasFull)
			m.fullHeaps.erase(pHeap);
		else
		{
			m.heaps[index].erase(pHeap);
			if (!m.heaps[index].begin())
				m.heapAvailabilityMask &= ~(1u << index);
		}

		objectPool.free(pHeap);
	}
	else if (wasFull)
	{
		m.heaps[newIndex].moveToFront(m.fullHeaps, pHeap);
		m.heapAvailabilityMask |= 1u << newIndex;
	}
	else if (index != newIndex)
	{
		m.heaps[newIndex].moveToFront(m.heaps[index], pHeap);
		m.heapAvailabilityMask |= 1u << newIndex;
		if (!m.heaps[index].begin())
			m.heapAvailabilityMask &= ~(1u << index);
	}
}

bool Allocator::allocate(uint32_t size, uint32_t alignment, AllocationTiling mode, DeviceAllocation *pAlloc)
{
	for (auto &c : classes)
	{
		// Find a suitable class to allocate from.
		if (size <= c.subBlockSize * Block::NumSubBlocks)
		{
			if (alignment > c.subBlockSize)
			{
				size_t paddedSize = size + alignment;
				if (paddedSize <= c.subBlockSize * Block::NumSubBlocks)
					size = paddedSize;
				else
					continue;
			}

			bool ret = c.allocate(size, mode, pAlloc, false);
#ifdef VULKAN_TEST
			if (ret)
				pAlloc->initCookie();
#endif
			return ret;
		}
	}

	MEMORY_ASSERT(0 && "Too large allocation!");
	return false;
}

Allocator::Allocator()
{
	for (unsigned i = 0; i < MEMORY_CLASS_COUNT - 1; i++)
		classes[i].setParent(&classes[i + 1]);

	get_class_allocator(MEMORY_CLASS_SMALL).setTilingMask(~0u);
	get_class_allocator(MEMORY_CLASS_MEDIUM).setTilingMask(~0u);
	get_class_allocator(MEMORY_CLASS_LARGE).setTilingMask(0);
	get_class_allocator(MEMORY_CLASS_HUGE).setTilingMask(0);

	get_class_allocator(MEMORY_CLASS_SMALL).setSubBlockSize(256);
	get_class_allocator(MEMORY_CLASS_MEDIUM).setSubBlockSize(256 * Block::NumSubBlocks); // 8K
	get_class_allocator(MEMORY_CLASS_LARGE).setSubBlockSize(256 * Block::NumSubBlocks * Block::NumSubBlocks); // 256K
	get_class_allocator(MEMORY_CLASS_HUGE)
	    .setSubBlockSize(256 * Block::NumSubBlocks * Block::NumSubBlocks * Block::NumSubBlocks / 2); // 4M
}

bool Mallocator::allocate(uint32_t size, uint32_t memoryType, Memory *pMemory, uint8_t **ppHostMemory)
{
	auto itr = find_if(begin(blocks), end(blocks),
	                   [=](const Allocation &alloc) { return size == alloc.size && memoryType == alloc.type; });

	if (itr != end(blocks))
	{
		*pMemory = itr->memory;
		*ppHostMemory = reinterpret_cast<uint8_t *>(itr->memory);
		blocks.erase(itr);
		return true;
	}

	*ppHostMemory = static_cast<uint8_t *>(malloc(size));
	*pMemory = reinterpret_cast<Memory>(*ppHostMemory);
	if (*ppHostMemory)
		return true;
	else
	{
		// TODO: Scavenge other blocks ...
		return false;
	}
}

void Mallocator::free(uint32_t size, uint32_t memoryType, Memory memory, uint8_t *)
{
	blocks.push_back({ memory, size, memoryType });
}

Mallocator::~Mallocator()
{
	for (auto &block : blocks)
		::free(reinterpret_cast<void *>(block.memory));
}

void DeviceAllocator::init(VkPhysicalDevice gpu, VkDevice vkdevice)
{
	device = vkdevice;
	vkGetPhysicalDeviceMemoryProperties(gpu, &memProps);

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(gpu, &props);
	atomAlignment = props.limits.nonCoherentAtomSize;

	heaps.clear();
	allocators.clear();

	heaps.resize(memProps.memoryHeapCount);
	for (unsigned i = 0; i < memProps.memoryTypeCount; i++)
	{
		allocators.emplace_back(new Allocator);
		allocators.back()->setMemoryType(i);
		allocators.back()->setGlobalAllocator(this);
	}
}

bool DeviceAllocator::allocate(uint32_t size, uint32_t alignment, uint32_t memoryType, AllocationTiling mode,
                               DeviceAllocation *pAlloc)
{
	return allocators[memoryType]->allocate(size, alignment, mode, pAlloc);
}

void DeviceAllocator::Heap::garbageCollect(VkDevice device)
{
	for (auto &block : blocks)
	{
		if (block.pHostMemory)
			vkUnmapMemory(device, block.memory);
		vkFreeMemory(device, block.memory, nullptr);
		size -= block.size;
	}
}

DeviceAllocator::~DeviceAllocator()
{
	for (auto &heap : heaps)
		heap.garbageCollect(device);
}

void DeviceAllocator::free(uint32_t size, uint32_t memoryType, Memory memory, uint8_t *pHostMemory)
{
	auto &heap = heaps[memProps.memoryTypes[memoryType].heapIndex];
#ifdef MEMORY_ALLOCATOR_THREAD_SAFE
	lock_guard<mutex> holder{ *heap.lock };
#endif
	heap.blocks.push_back({ reinterpret_cast<VkDeviceMemory>(memory), pHostMemory, size, memoryType });
}

void DeviceAllocator::garbageCollect()
{
	for (auto &heap : heaps)
	{
#ifdef MEMORY_ALLOCATOR_THREAD_SAFE
		lock_guard<mutex> holder{ *heap.lock };
#endif
		heap.garbageCollect(device);
	}
}

void *DeviceAllocator::mapMemory(DeviceAllocation *pAlloc, MemoryAccessFlags flags)
{
	// This will only happen if the memory type is device local only, which we cannot possibly map.
	if (!pAlloc->pHostBase)
		return nullptr;

	pAlloc->accessFlags = flags;

	if ((flags & MEMORY_ACCESS_READ) &&
	    !(memProps.memoryTypes[pAlloc->memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VkDeviceSize offset = pAlloc->offset & ~(atomAlignment - 1);
		VkDeviceSize size = (pAlloc->offset + pAlloc->getSize() - offset + atomAlignment - 1) & ~(atomAlignment - 1);

		// Have to invalidate cache here.
		const VkMappedMemoryRange range = {
			VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
			nullptr,
			reinterpret_cast<VkDeviceMemory>(pAlloc->base),
			offset,
			size,
		};
		vkInvalidateMappedMemoryRanges(device, 1, &range);
	}

	return pAlloc->pHostBase;
}

void DeviceAllocator::unmapMemory(const DeviceAllocation &alloc)
{
	// This will only happen if the memory type is device local only, which we cannot possibly map.
	if (!alloc.pHostBase)
		return;

	if ((alloc.accessFlags & MEMORY_ACCESS_WRITE) &&
	    !(memProps.memoryTypes[alloc.memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VkDeviceSize offset = alloc.offset & ~(atomAlignment - 1);
		VkDeviceSize size = (alloc.offset + alloc.getSize() - offset + atomAlignment - 1) & ~(atomAlignment - 1);

		// Have to flush caches here.
		const VkMappedMemoryRange range = {
			VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, reinterpret_cast<VkDeviceMemory>(alloc.base), offset, size,
		};
		vkFlushMappedMemoryRanges(device, 1, &range);
	}
}

bool DeviceAllocator::allocate(uint32_t size, uint32_t memoryType, Memory *pMemory, uint8_t **ppHostMemory)
{
	auto &heap = heaps[memProps.memoryTypes[memoryType].heapIndex];
#ifdef MEMORY_ALLOCATOR_THREAD_SAFE
	lock_guard<mutex> holder{ *heap.lock };
#endif

	// Naive searching is fine here as vkAllocate blocks are *huge* and we won't have many of them.
	auto itr = find_if(begin(heap.blocks), end(heap.blocks),
	                   [=](const Allocation &alloc) { return size == alloc.size && memoryType == alloc.type; });

	bool hostVisible = (memProps.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

	// Found previously used block.
	if (itr != end(heap.blocks))
	{
		*pMemory = reinterpret_cast<Memory>(itr->memory);
		*ppHostMemory = itr->pHostMemory;
		heap.blocks.erase(itr);
		return true;
	}

	VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, size, memoryType };
	VkDeviceMemory deviceMemory;
	VkResult res = vkAllocateMemory(device, &info, nullptr, &deviceMemory);

	if (res == VK_SUCCESS)
	{
		heap.size += size;
		*pMemory = reinterpret_cast<Memory>(deviceMemory);

		if (hostVisible)
		{
			if (vkMapMemory(device, deviceMemory, 0, size, 0, reinterpret_cast<void **>(ppHostMemory)) != VK_SUCCESS)
				return false;
		}

		return true;
	}
	else
	{
		// Look through our heap and see if there are blocks of other types we can free.
		auto itr = begin(heap.blocks);
		while (res != VK_SUCCESS && itr != end(heap.blocks))
		{
			if (itr->pHostMemory)
				vkUnmapMemory(device, itr->memory);
			vkFreeMemory(device, itr->memory, nullptr);
			heap.size -= itr->size;
			res = vkAllocateMemory(device, &info, nullptr, &deviceMemory);
			++itr;
		}

		heap.blocks.erase(begin(heap.blocks), itr);

		if (res == VK_SUCCESS)
		{
			heap.size += size;
			*pMemory = reinterpret_cast<Memory>(deviceMemory);

			if (hostVisible)
			{
				if (vkMapMemory(device, deviceMemory, 0, size, 0, reinterpret_cast<void **>(ppHostMemory)) !=
				    VK_SUCCESS)
				{
					vkFreeMemory(device, deviceMemory, nullptr);
					return false;
				}
			}

			return true;
		}
		else
			return false;
	}
}
}
