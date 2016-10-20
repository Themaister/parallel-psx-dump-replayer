#pragma once

#include "hashmap.hpp"
#include "object_pool.hpp"
#include "vulkan.hpp"
#include <utility>
#include <vector>

namespace Vulkan
{
class Device;
struct DescriptorSetLayout
{
	uint32_t sampled_image_mask = 0;
	uint32_t storage_image_mask = 0;
	uint32_t uniform_buffer_mask = 0;
	uint32_t storage_buffer_mask = 0;
	VkShaderStageFlags stages = 0;
};

static const unsigned VULKAN_NUM_SETS_PER_POOL = 16;
static const unsigned VULKAN_DESCRIPTOR_RING_SIZE = 8;

class DescriptorSetAllocator
{
public:
	DescriptorSetAllocator(Device *device, const DescriptorSetLayout &layout);
	~DescriptorSetAllocator();
	void operator=(const DescriptorSetAllocator &) = delete;
	DescriptorSetAllocator(const DescriptorSetAllocator &) = delete;

	void begin_frame();
	std::pair<VkDescriptorSet, bool> find(Hash hash);

	VkDescriptorSetLayout get_layout() const
	{
		return set_layout;
	}

private:
	struct DescriptorSetNode : IntrusiveListEnabled<DescriptorSetNode>
	{
		DescriptorSetNode(VkDescriptorSet set)
		    : set(set)
		{
		}

		VkDescriptorSet set;
		Hash hash = 0;
		unsigned index = 0;
	};

	Device *device;
	VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;

	WeakList<DescriptorSetNode> rings[VULKAN_DESCRIPTOR_RING_SIZE];
	ObjectPool<DescriptorSetNode> object_pool;
	unsigned index = 0;

	std::vector<WeakList<DescriptorSetNode>::Iterator> vacant;
	std::vector<VkDescriptorPoolSize> pool_size;
	std::vector<VkDescriptorPool> pools;
	HashMap<WeakList<DescriptorSetNode>::Iterator> set_nodes;
};
}
