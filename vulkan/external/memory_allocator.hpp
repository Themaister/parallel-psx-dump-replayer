/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2016 ARM Limited
 *     ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef FRAMEWORK_MEMORY_ALLOCATOR_HPP
#define FRAMEWORK_MEMORY_ALLOCATOR_HPP

#include <assert.h>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "vulkan.hpp"

#ifdef MEMORY_ALLOCATOR_THREAD_SAFE
#include <mutex>
#endif

namespace MaliSDK
{
using Memory = uint64_t;

static inline uint32_t nextPow2(uint32_t v)
{
	v--;
	v |= v >> 16;
	v |= v >> 8;
	v |= v >> 4;
	v |= v >> 2;
	v |= v >> 1;
	v++;
	return v;
}

#ifdef __GNUC__
static inline uint32_t ctz(uint32_t v)
{
	return v == 0 ? 32 : __builtin_ctz(v);
}

static inline uint32_t clz(uint32_t v)
{
	return v == 0 ? 32 : __builtin_clz(v);
}

static inline uint32_t popcount(uint32_t v)
{
	return __builtin_popcount(v);
}
#else
#error "Implement ctz, clz, popcount for other platforms."
#endif

static inline uint32_t log2Integer(uint32_t v)
{
	v--;
	return 32 - clz(v);
}

/// @brief Represents a node in the HeapList.
template <typename T>
struct HeapListNode
{
	/// Link to next node.
	struct HeapListNode<T> *pNext;
	/// Link to previous node.
	struct HeapListNode<T> *pPrev;
	/// Data storage.
	T storage;
};

/// @brief Simplified list implementation which supports bare minimum needed for ClassAllocator.
template <typename T>
class HeapList
{
public:
	/// @brief Destructor
	~HeapList()
	{
		if (pHead)
			LOG("Memory leak in HeapList detected.\n");
	}

	/// Helper typedef.
	using NodeType = HeapListNode<T>;

	/// @brief Returns head node.
	/// @returns first node or nullptr if non-existent.
	NodeType *begin() const
	{
		return pHead;
	}

	/// @brief Inserts node to front of list.
	/// @param pNode Node.
	void insertFront(NodeType *pNode)
	{
		pNode->pNext = pHead;
		pNode->pPrev = nullptr;

		if (pHead)
			pHead->pPrev = pNode;
		pHead = pNode;

		listSize++;
	}

	/// @brief Moves node from other list to front of this list.
	/// @param other Other list which to move from.
	/// @param pNode Node to move.
	void moveToFront(HeapList<T> &other, NodeType *pNode)
	{
		other.erase(pNode);
		insertFront(pNode);
	}

	/// @brief Erases node from list.
	/// @param pNode Node to remove.
	void erase(NodeType *pNode)
	{
		auto *pPrev = pNode->pPrev;
		auto *pNext = pNode->pNext;

		if (pPrev)
			pPrev->pNext = pNode->pNext;
		else
			pHead = pNode->pNext;

		if (pNext)
			pNext->pPrev = pPrev;

		listSize--;
	}

	/// @brief Returns number of entries in the list.
	/// @returns size
	size_t size() const
	{
		return listSize;
	}

private:
	HeapListNode<T> *pHead = nullptr;
	size_t listSize = 0;
};

/// @brief A simple object pool.
template <typename T>
class ObjectPool
{
public:
	/// @brief Allocates and default constructs a new object.
	/// @returns Pointer to object, must be freed.
	T *allocate()
	{
		T *block;
		if (vacant.empty())
		{
			unsigned allocateNodes = 1024 << backingStorage.size();
			totalNodes += allocateNodes;
			vacant.reserve(totalNodes);

			// We need malloc/free style allocation here since we are going to
			// construct/destruct the objects multiple times.
			T *pNewBlocks = static_cast<T *>(malloc(allocateNodes * sizeof(T)));
			if (!pNewBlocks)
				return nullptr;

			for (unsigned i = 0; i < allocateNodes; i++)
				vacant.push_back(&pNewBlocks[i]);

			backingStorage.emplace_back(pNewBlocks);
		}

		block = vacant.back();
		vacant.pop_back();

		new (block) T();
		return block;
	}

	/// @brief Frees previously allocated object back to pool.
	/// @param obj Object to free.
	void free(T *obj)
	{
		obj->~T();
		vacant.push_back(obj);
	}

	/// @brief Destructor.
	~ObjectPool()
	{
		if (vacant.size() != totalNodes)
			LOG("ObjectPool memory leak detected (%zu != %zu).\n", vacant.size(), totalNodes);
	}

private:
	struct MallocDeleter
	{
		void operator()(T *ptr)
		{
			::free(ptr);
		}
	};
	std::vector<T *> vacant;
	std::vector<std::unique_ptr<T, MallocDeleter>> backingStorage;
	size_t totalNodes = 0;
};

/// @brief Various memory classes to allocate.
enum MemoryClass
{
	/// Small sizes, by default, [256B, 8KiB)
	MEMORY_CLASS_SMALL = 0,
	/// Medium sizes, by default, [8KiB, 256KiB)
	MEMORY_CLASS_MEDIUM,
	/// Large sizes, by default, [256KiB, 8MiB)
	MEMORY_CLASS_LARGE,
	/// Huge sizes, by default, [4MiB, 128MiB)
	MEMORY_CLASS_HUGE,
	/// Number of memory classes
	MEMORY_CLASS_COUNT
};

/// @brief Allocation tiling modes
enum AllocationTiling
{
	/// Linear tiling, used for VkBuffer and VkImage with linear tiling
	ALLOCATION_TILING_LINEAR = 0,
	/// Optimal tiling, used for VkImage with optimal tiling
	ALLOCATION_TILING_OPTIMAL,
	/// Number of allocation tiling modes
	ALLOCATION_TILING_COUNT
};

/// @brief Memory access flags for memory mapping.
enum MemoryAccessFlag
{
	/// Write enabled
	MEMORY_ACCESS_WRITE = 1,
	/// Read enabled
	MEMORY_ACCESS_READ = 2,
	/// Read/Write enabled
	MEMORY_ACCESS_READ_WRITE = MEMORY_ACCESS_WRITE | MEMORY_ACCESS_READ
};
/// @brief Simple typedef for bitmask of MemoryAccessFlag.
using MemoryAccessFlags = uint32_t;

/// @brief A global allocator interface. The interface is designed for fixed-size, huge allocations.
class GlobalAllocator
{
public:
	/// @brief Destructor
	virtual ~GlobalAllocator() = default;

	/// @brief Allocate global memory.
	/// @param size Size of the allocation.
	/// @param memoryType The memory type of the allocation.
	/// @param[out] pMemory Allocated memory.
	/// @param[out] ppHostMemory Host memory if the memory type supports host mapping.
	virtual bool allocate(uint32_t size, uint32_t memoryType, Memory *pMemory, uint8_t **ppHostMemory) = 0;

	/// @brief Free global memory.
	/// @param size Size of the allocation.
	/// @param memoryType The memory type of the allocation.
	/// @param memory Previously allocated memory.
	/// @param pHostMemory Host memory if the memory type supports host mapping.
	virtual void free(uint32_t size, uint32_t memoryType, Memory memory, uint8_t *pHostMemory) = 0;
};

/// @brief An implementation of GlobalAllocator which uses malloc/free, used mostly for testing.
class Mallocator : public GlobalAllocator
{
public:
	/// @brief Destructor
	~Mallocator();

	/// @brief Allocate global memory.
	/// @param size Size of the allocation.
	/// @param memoryType The memory type of the allocation.
	/// @param[out] pMemory Allocated memory.
	/// @param[out] ppHostMemory Host memory if the memory type supports host mapping.
	bool allocate(uint32_t size, uint32_t memoryType, Memory *pMemory, uint8_t **ppHostMemory) override;

	/// @brief Free global memory.
	/// @param size Size of the allocation.
	/// @param memoryType The memory type of the allocation.
	/// @param memory Previously allocated memory.
	/// @param pHostMemory Host memory if the memory type supports host mapping.
	void free(uint32_t size, uint32_t memoryType, Memory memory, uint8_t *pHostMemory) override;

private:
	struct Allocation
	{
		Memory memory;
		uint32_t size;
		uint32_t type;
	};

	std::vector<Allocation> blocks;
};

struct DeviceAllocation;

/// @brief MiniHeap allocator structure. Holds 32 entries.
class Block
{
public:
	enum
	{
		/// The number of sub blocks held in this heap.
		NumSubBlocks = 32u,
		/// Bitmask representing that all entries are free.
		AllFree = ~0u
	};

	/// @brief Disable copy/move.
	Block(const Block &) = delete;
	/// @brief Disable copy/move.
	void operator=(const Block &) = delete;

	/// @brief Constructor
	Block()
	{
		for (auto &v : freeBlocks)
			v = AllFree;
		longestRun = 32;
	}

	/// @brief Destructor
	~Block()
	{
		if (freeBlocks[0] != AllFree)
			LOG("Memory leak in block detected.\n");
	}

	/// @brief Check if the allocator is full.
	/// @returns true if full, false otherwise.
	inline bool full() const
	{
		return freeBlocks[0] == 0;
	}

	/// @brief Check if the allocator is completely vacant.
	/// @returns true if completely vacant, false otherwise.
	inline bool empty() const
	{
		return freeBlocks[0] == AllFree;
	}

	/// @brief Gets the largest number of consecutive blocks which can be allocated.
	/// @returns Largest number of consecutive blocks which can be allocated.
	inline uint32_t getLongestRun() const
	{
		return longestRun;
	}

	/// @brief Allocate blocks from small heap.
	///
	/// This function must only be called if numBlocks <= getLongestRun().
	///
	/// @param numBlocks The number of blocks to allocate.
	/// @param[out] pBlock The allocation.
	void allocate(uint32_t numBlocks, DeviceAllocation *pBlock);

	/// @brief Frees a previously allocated bitmask.
	/// @param mask Allocation bitmask.
	void free(uint32_t mask);

private:
	uint32_t freeBlocks[NumSubBlocks];
	uint32_t longestRun = 0;

	inline void updateLongestRun()
	{
		uint32_t f = freeBlocks[0];
		longestRun = 0;

		while (f)
		{
			freeBlocks[longestRun++] = f;
			f &= f >> 1;
		}
	}
};

struct MiniHeap;
class ClassAllocator;
class DeviceAllocator;
class Allocator;

/// @brief A device allocation.
struct DeviceAllocation
{
	friend class ClassAllocator;
	friend class Allocator;
	friend class Block;
	friend class DeviceAllocator;

public:
	/// @brief Gets the memory object for this allocation.
	/// @returns Device memory
	inline Memory getMemory() const
	{
		return base;
	}

	/// @brief Gets the offset into device memory.
	/// @returns Memory offset in bytes.
	inline uint32_t getOffset() const
	{
		return offset;
	}

	/// @brief Gets the allocated size for this buffer.
	/// @returns Size in bytes. Might be slightly larger than the requested size.
	inline uint32_t getSize() const
	{
		return size;
	}

	/// @brief Gets the internal bitmask of the allocation.
	/// @returns Allocation bitmask.
	inline uint32_t getMask() const
	{
		return mask;
	}

	/// @brief Immediately frees the allocation. Should only be used internally.
	/// Freeing this memory will not guarantee that the device is no longer in use.
	void freeImmediate();

private:
	Memory base = {};
	uint8_t *pHostBase = nullptr;
	ClassAllocator *pAlloc = nullptr;
	HeapList<MiniHeap>::NodeType *pHeap = nullptr;
	uint32_t offset = 0;
	uint32_t mask = 0;
	uint32_t size = 0;

	bool hierarchical = false;
	uint8_t tiling = 0;
	uint8_t memoryType = 0;
	uint8_t accessFlags = 0;

#ifdef VULKAN_TEST
	uint32_t cookie = 0;
	void initCookie();
#endif

	void freeGlobal(GlobalAllocator &allocator, uint32_t size, uint32_t memoryType);

	inline uint8_t *getHostMemory() const
	{
		return pHostBase;
	}
};

/// @brief A small 32-entry heap.
struct MiniHeap
{
	/// @brief The allocation which backs this heap.
	DeviceAllocation allocation;
	/// @brief The heap allocator.
	Block heap;
};

class Allocator;

/// @brief An allocator designed for a specific size. Used as part of the hierarchical allocation scheme.
class ClassAllocator
{
public:
	friend class Allocator;

	/// @brief Destructor
	~ClassAllocator();

#ifdef VULKAN_TEST
	/// @brief Gets the occupied size of this allocator.
	/// @returns Occupied size.
	inline uint64_t getOccupiedSize() const
	{
		return occupiedSize;
	}

	/// @brief Gets the occupied size for hierarchical allocations from child allocators.
	/// @returns Occupied size.
	inline uint64_t getHierarchicalOccupiedSize() const
	{
		return hierOccupiedSize;
	}
#endif

	/// @brief Sets the tiling mode mask for this allocator.
	///
	/// If the sub block size of the allocator is large enough, no alignment requirements
	/// are needed for linear vs. optimally tiled allocations.
	///
	/// @param mask Tiling mask. 0 can be used to ignore the tiling mode (sub block size >= bufferImageGranularity),
	/// all bits should be set to 1 otherwise.
	inline void setTilingMask(uint32_t mask)
	{
		tilingMask = mask;
	}

	/// @brief Returns the total allocated size in this allocator.
	/// @returns Allocated size
	inline uint64_t getConsumedSize() const
	{
		uint64_t size = 0;
		for (auto &m : tilingModes)
		{
			for (auto &h : m.heaps)
				size += h.size();
			size += m.fullHeaps.size();
		}
		return size * subBlockSize * Block::NumSubBlocks;
	}

	/// @brief Sets the sub block size of this allocator.
	/// @param size The sub block size for this allocator.
	inline void setSubBlockSize(uint32_t size)
	{
		subBlockSizeLog2 = log2Integer(size);
		subBlockSize = size;
	}

	/// @brief Allocates memory with size, tiling mode.
	/// @param size The number of bytes to allocate. The allocation has a maximum allocation size.
	/// @param tiling The tiling mode of the allocation, used to give higher alignment requirements to optimal vs. linear tiled images.
	/// @param[out] pAlloc The allocation
	/// @param hierarchical True if the allocation comes from a child allocator, false if not.
	/// @returns true if allocation succeeds, false otherwise.
	bool allocate(uint32_t size, AllocationTiling tiling, DeviceAllocation *pAlloc, bool hierarchical);

	/// @brief Frees an allocation. Used internally.
	/// @param[in] pAlloc The allocation. It must have been allocated from this class allocator.
	void free(DeviceAllocation *pAlloc);

private:
	ClassAllocator() = default;
	struct AllocationTilingHeaps
	{
		HeapList<MiniHeap> heaps[Block::NumSubBlocks];
		HeapList<MiniHeap> fullHeaps;
		uint32_t heapAvailabilityMask = 0;
	};
	ClassAllocator *pParent = nullptr;
	AllocationTilingHeaps tilingModes[ALLOCATION_TILING_COUNT];
	ObjectPool<typename HeapList<MiniHeap>::NodeType> objectPool;

	uint32_t subBlockSize = 1;
	uint32_t subBlockSizeLog2 = 0;
	uint32_t tilingMask = ~0u;
	uint32_t memoryType = 0;
#ifdef MEMORY_ALLOCATOR_THREAD_SAFE
	std::mutex lock;
#endif
	GlobalAllocator *pGlobalAllocator = nullptr;

	void setGlobalAllocator(GlobalAllocator *pAllocator)
	{
		pGlobalAllocator = pAllocator;
	}

	void setMemoryType(uint32_t type)
	{
		memoryType = type;
	}

#ifdef VULKAN_TEST
	uint64_t occupiedSize = 0;
	uint64_t hierOccupiedSize = 0;
#endif

	void suballocate(uint32_t numBlocks, uint32_t tiling, uint32_t memoryType, MiniHeap &heap,
	                 DeviceAllocation *pAlloc);

	inline void setParent(ClassAllocator *pAllocator)
	{
		pParent = pAllocator;
	}
};

/// @brief The high-level allocator. Plugs together a bunch of class allocators to form a hierarchical allocator.
class Allocator
{
public:
	/// @brief Constructor
	Allocator();
	void operator=(const Allocator &) = delete;
	Allocator(const Allocator &) = delete;

	/// @brief Allocates memory with size, tiling mode.
	/// @param size The number of bytes to allocate. The allocation has a maximum allocation size.
	/// @param tiling The tiling mode of the allocation, used to give higher alignment requirements to optimal vs. linear tiled images.
	/// @param[out] pAlloc The allocation
	/// @returns true if allocation succeeds, false otherwise.
	bool allocate(uint32_t size, AllocationTiling tiling, DeviceAllocation *pAlloc);

	/// @brief Gets a class allocator
	/// @param clazz The allocation class.
	/// @returns class allocator.
	inline ClassAllocator &get_class_allocator(MemoryClass clazz)
	{
		return classes[static_cast<unsigned>(clazz)];
	}

	/// @brief Frees an allocation
	/// @param pAlloc Allocation
	static void free(DeviceAllocation *pAlloc)
	{
		pAlloc->freeImmediate();
	}

#ifdef VULKAN_TEST
	/// @brief Gets the occupied size of all allocators.
	/// @returns Occupied size.
	inline uint64_t getOccupiedSize() const
	{
		uint64_t occupied = 0;
		for (auto &sub : classes)
			occupied += sub.getOccupiedSize();
		return occupied;
	}
#endif

	/// @brief Sets the memory type used for this allocator.
	/// @param memoryType The memory type.
	void setMemoryType(uint32_t memoryType)
	{
		for (auto &sub : classes)
			sub.setMemoryType(memoryType);
	}

	/// @brief Sets the global fallback allocator.
	/// @param pAllocator The allocator.
	void setGlobalAllocator(GlobalAllocator *pAllocator)
	{
		for (auto &sub : classes)
			sub.setGlobalAllocator(pAllocator);
	}

private:
	ClassAllocator classes[MEMORY_CLASS_COUNT];
};

/// @brief An allocator for VkDeviceMemory. It builds on top of Allocator.
class DeviceAllocator : public GlobalAllocator
{
public:
	/// @brief Initializes the allocator. Must only be called once.
	/// @param gpu The Vulkan physical device.
	/// @param device The Vulkan device.
	void init(VkPhysicalDevice gpu, VkDevice device);

	/// @brief Destructor
	~DeviceAllocator();

	/// @brief Allocates Vulkan device memory. Alignment is implied.
	/// @param size The desired size in bytes.
	/// @param memoryType The memory type.
	/// @param tiling The tiling mode for this allocation.
	/// @param[out] pAlloc The allocation.
	/// @returns True is allocation succeeded, false otherwise.
	bool allocate(uint32_t size, uint32_t memoryType, AllocationTiling tiling, DeviceAllocation *pAlloc);

	/// @brief Reclaims all currently unused memory blocks.
	///
	/// This operation is blocking and should only be used in scenarios where this is acceptable.
	void garbageCollect();

	/// @brief Maps device memory.
	///
	/// This function cannot fail and completes immediately as host visible device memory is mapped on allocation.
	/// Mapping is explicit as this call will invalidate caches if incoherent memory is used.
	///
	/// @param[in,out] pAlloc An allocation.
	/// @param flags Memory access flags for this mapping.
	/// @returns Pointer to host memory.
	void *mapMemory(DeviceAllocation *pAlloc, MemoryAccessFlags flags);

	/// @brief Unmaps device memory.
	///
	/// Flushes CPU caches if the host mapped memory is incoherent with device memory.
	/// @param[in] alloc An allocation which was previously mapped.
	void unmapMemory(const DeviceAllocation &alloc);

private:
	std::vector<std::unique_ptr<Allocator>> allocators;
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDeviceMemoryProperties memProps;
	VkDeviceSize atomAlignment = 1;

	bool allocate(uint32_t size, uint32_t memoryType, Memory *memory, uint8_t **ppHostMemory) override;
	void free(uint32_t size, uint32_t memoryType, Memory memory, uint8_t *pHostMemory) override;

	struct Allocation
	{
		VkDeviceMemory memory;
		uint8_t *pHostMemory;
		uint32_t size;
		uint32_t type;
	};

	struct Heap
	{
		uint64_t size = 0;
#ifdef MEMORY_ALLOCATOR_THREAD_SAFE
		std::unique_ptr<std::mutex> lock{ new std::mutex };
#endif
		std::vector<Allocation> blocks;

		void garbageCollect(VkDevice device);
	};

	std::vector<Heap> heaps;
};
}

#endif
