#pragma once

#include "vulkan.hpp"
#include "buffer.hpp"
#include <vector>

namespace Vulkan
{
class Device;
struct ChainDataAllocation
{
	const Buffer *buffer;
	VkDeviceSize offset;
	void *data;
};

class ChainAllocator
{
public:
	ChainAllocator(Device *device, VkDeviceSize block_size, VkDeviceSize alignment, VkBufferUsageFlags usage);
	~ChainAllocator();

	ChainDataAllocation allocate(VkDeviceSize size);
	void discard();
	void reset();
	void flush();

private:
	Device *device;
	VkDeviceSize block_size;
	VkDeviceSize alignment;
	VkBufferUsageFlags usage;

	std::vector<BufferHandle> buffers;
	unsigned chain_index = 0;
	VkDeviceSize offset = 0;
	VkDeviceSize size = 0;
	uint8_t *host = nullptr;
};

}
