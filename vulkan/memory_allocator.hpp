#ifndef FRAMEWORK_MEMORY_ALLOCATOR_HPP
#define FRAMEWORK_MEMORY_ALLOCATOR_HPP

#include "intrusive.hpp"
#include "object_pool.hpp"
#include "vulkan.hpp"
#include <assert.h>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <vector>

#ifdef MEMORY_ALLOCATOR_THREAD_SAFE
#include <mutex>
#endif

namespace Vulkan
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

enum MemoryClass
{
	MEMORY_CLASS_SMALL = 0,
	MEMORY_CLASS_MEDIUM,
	MEMORY_CLASS_LARGE,
	MEMORY_CLASS_HUGE,
	MEMORY_CLASS_COUNT
};

enum AllocationTiling
{
	ALLOCATION_TILING_LINEAR = 0,
	ALLOCATION_TILING_OPTIMAL,
	ALLOCATION_TILING_COUNT
};

enum MemoryAccessFlag
{
	MEMORY_ACCESS_WRITE = 1,
	MEMORY_ACCESS_READ = 2,
	MEMORY_ACCESS_READ_WRITE = MEMORY_ACCESS_WRITE | MEMORY_ACCESS_READ
};
using MemoryAccessFlags = uint32_t;

class GlobalAllocator
{
public:
	virtual ~GlobalAllocator() = default;
	virtual bool allocate(uint32_t size, uint32_t memoryType, Memory *pMemory, uint8_t **ppHostMemory) = 0;
	virtual void free(uint32_t size, uint32_t memoryType, Memory memory, uint8_t *pHostMemory) = 0;
	virtual void freeNoRecycle(uint32_t size, uint32_t memoryType, Memory memory, uint8_t *pHostMemory) = 0;
};

struct DeviceAllocation;

class Block
{
public:
	enum
	{
		NumSubBlocks = 32u,
		AllFree = ~0u
	};

	Block(const Block &) = delete;
	void operator=(const Block &) = delete;

	Block()
	{
		for (auto &v : freeBlocks)
			v = AllFree;
		longestRun = 32;
	}

	~Block()
	{
		if (freeBlocks[0] != AllFree)
			LOG("Memory leak in block detected.\n");
	}

	inline bool full() const
	{
		return freeBlocks[0] == 0;
	}

	inline bool empty() const
	{
		return freeBlocks[0] == AllFree;
	}

	inline uint32_t getLongestRun() const
	{
		return longestRun;
	}

	void allocate(uint32_t numBlocks, DeviceAllocation *pBlock);
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

struct DeviceAllocation
{
	friend class ClassAllocator;
	friend class Allocator;
	friend class Block;
	friend class DeviceAllocator;

public:
	inline Memory getMemory() const
	{
		return base;
	}

	inline bool allocationIsGlobal() const
	{
		return !pAlloc && base;
	}

	inline VkDeviceMemory getDeviceMemory() const
	{
		return reinterpret_cast<VkDeviceMemory>(base);
	}

	inline uint32_t getOffset() const
	{
		return offset;
	}

	inline uint32_t getSize() const
	{
		return size;
	}

	inline uint32_t getMask() const
	{
		return mask;
	}

	void freeImmediate();
	void freeImmediate(GlobalAllocator &allocator);

private:
	Memory base = {};
	uint8_t *pHostBase = nullptr;
	ClassAllocator *pAlloc = nullptr;
	IntrusiveList<MiniHeap>::Iterator heap = {};
	uint32_t offset = 0;
	uint32_t mask = 0;
	uint32_t size = 0;

	bool hierarchical = false;
	uint8_t tiling = 0;
	uint8_t memoryType = 0;
	uint8_t accessFlags = 0;

	void freeGlobal(GlobalAllocator &allocator, uint32_t size, uint32_t memoryType);

	inline uint8_t *getHostMemory() const
	{
		return pHostBase;
	}
};

struct MiniHeap : IntrusiveListEnabled<MiniHeap>
{
	DeviceAllocation allocation;
	Block heap;
};

class Allocator;

class ClassAllocator
{
public:
	friend class Allocator;
	~ClassAllocator();

	inline void setTilingMask(uint32_t mask)
	{
		tilingMask = mask;
	}

	inline void setSubBlockSize(uint32_t size)
	{
		subBlockSizeLog2 = log2Integer(size);
		subBlockSize = size;
	}

	bool allocate(uint32_t size, AllocationTiling tiling, DeviceAllocation *pAlloc, bool hierarchical);
	void free(DeviceAllocation *pAlloc);

private:
	ClassAllocator() = default;
	struct AllocationTilingHeaps
	{
		IntrusiveList<MiniHeap> heaps[Block::NumSubBlocks];
		IntrusiveList<MiniHeap> fullHeaps;
		uint32_t heapAvailabilityMask = 0;
	};
	ClassAllocator *pParent = nullptr;
	AllocationTilingHeaps tilingModes[ALLOCATION_TILING_COUNT];
	ObjectPool<MiniHeap> objectPool;

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

	void suballocate(uint32_t numBlocks, uint32_t tiling, uint32_t memoryType, MiniHeap &heap,
	                 DeviceAllocation *pAlloc);

	inline void setParent(ClassAllocator *pAllocator)
	{
		pParent = pAllocator;
	}
};

class Allocator
{
public:
	Allocator();
	void operator=(const Allocator &) = delete;
	Allocator(const Allocator &) = delete;

	bool allocate(uint32_t size, uint32_t alignment, AllocationTiling tiling, DeviceAllocation *pAlloc);

	bool allocateGlobal(uint32_t size, DeviceAllocation *pAlloc);

	inline ClassAllocator &get_class_allocator(MemoryClass clazz)
	{
		return classes[static_cast<unsigned>(clazz)];
	}

	static void free(DeviceAllocation *pAlloc)
	{
		pAlloc->freeImmediate();
	}

	void setMemoryType(uint32_t memoryType)
	{
		for (auto &sub : classes)
			sub.setMemoryType(memoryType);
		this->memoryType = memoryType;
	}

	void setGlobalAllocator(GlobalAllocator *pAllocator)
	{
		for (auto &sub : classes)
			sub.setGlobalAllocator(pAllocator);
		pGlobalAllocator = pAllocator;
	}

private:
	ClassAllocator classes[MEMORY_CLASS_COUNT];
	GlobalAllocator *pGlobalAllocator = nullptr;
	uint32_t memoryType = 0;
};

class DeviceAllocator : public GlobalAllocator
{
public:
	void init(VkPhysicalDevice gpu, VkDevice device);

	~DeviceAllocator();

	bool allocate(uint32_t size, uint32_t alignment, uint32_t memoryType, AllocationTiling tiling,
	              DeviceAllocation *pAlloc);

	bool allocateGlobal(uint32_t size, uint32_t memoryType, DeviceAllocation *pAlloc);

	void garbageCollect();
	void *mapMemory(DeviceAllocation *pAlloc, MemoryAccessFlags flags);
	void unmapMemory(const DeviceAllocation &alloc);

private:
	std::vector<std::unique_ptr<Allocator>> allocators;
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDeviceMemoryProperties memProps;
	VkDeviceSize atomAlignment = 1;

	bool allocate(uint32_t size, uint32_t memoryType, Memory *memory, uint8_t **ppHostMemory) override;
	void free(uint32_t size, uint32_t memoryType, Memory memory, uint8_t *pHostMemory) override;
	void freeNoRecycle(uint32_t size, uint32_t memoryType, Memory memory, uint8_t *pHostMemory) override;

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
