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
	ChainAllocator(Device *device, VkDeviceSize block_size, VkBufferUsageFlags usage);
	~ChainAllocator();

	ChainDataAllocation allocate(VkDeviceSize size);
	void flush();

private:
	std::vector<BufferHandle> buffers;
	unsigned chain_index = 0;
	VkDeviceSize offset = 0;
	VkDeviceSize size = 0;
	VkDeviceSize block_size;
	uint8_t *host = nullptr;
};

}
