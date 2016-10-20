#pragma once

#include "hashmap.hpp"
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
static const unsigned VULKAN_RING_SIZE = 8;

class DescriptorSetAllocator
{
public:
	DescriptorSetAllocator(Device *device, const DescriptorSetLayout &layout);
	~DescriptorSetAllocator();
	void operator=(const DescriptorSetAllocator &) = delete;
	DescriptorSetAllocator(const DescriptorSetAllocator &) = delete;

	void begin();
	std::pair<VkDescriptorSet, bool> find(Hash hash);

	VkDescriptorSetLayout get_layout() const
	{
		return set_layout;
	}

private:
	struct DescriptorSetNode
	{
		VkDescriptorSet set;
	};

	Device *device;
	VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> vacant;
	std::vector<VkDescriptorPoolSize> pool_size;
	std::vector<VkDescriptorPool> pools;
	HashMap<DescriptorSetNode> set_nodes;
};
}
