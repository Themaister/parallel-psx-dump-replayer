#include "descriptor_set.hpp"
#include "device.hpp"
#include <vector>

using namespace std;

namespace Vulkan
{
DescriptorSetAllocator::DescriptorSetAllocator(Device *device, const DescriptorSetLayout &layout)
    : device(device)
{
	VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };

	vector<VkDescriptorSetLayoutBinding> bindings;
	for (unsigned i = 0; i < VULKAN_NUM_BINDINGS; i++)
	{
		unsigned types = 0;
		if (layout.sampled_image_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, layout.stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.storage_image_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, layout.stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.uniform_buffer_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, layout.stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.storage_buffer_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, layout.stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		(void)types;
		VK_ASSERT(types <= 1 && "Descriptor set aliasing!");
	}

	if (!bindings.empty())
	{
		info.bindingCount = bindings.size();
		info.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(device->get_device(), &info, nullptr, &set_layout) != VK_SUCCESS)
			LOG("Failed to create descriptor set layout.");
	}
}

void DescriptorSetAllocator::begin_frame()
{
	index = (index + 1) & (VULKAN_DESCRIPTOR_RING_SIZE - 1);
	for (auto itr = begin(rings[index]); itr != end(rings[index]); ++itr)
	{
		vacant.push_back(itr);
		set_nodes.erase(itr->hash);
	}
	rings[index].clear();
}

pair<VkDescriptorSet, bool> DescriptorSetAllocator::find(Hash hash)
{
	auto itr = set_nodes.find(hash);
	if (itr != end(set_nodes))
	{
		auto i = itr->second;
		if (i->index != index)
		{
			rings[index].move_to_front(rings[i->index], i);
			i->index = index;
		}

		return { i->set, true };
	}
	else
	{
		if (vacant.empty())
		{
			VkDescriptorPool pool;
			VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			info.maxSets = VULKAN_NUM_SETS_PER_POOL;
			if (!pool_size.empty())
			{
				info.poolSizeCount = pool_size.size();
				info.pPoolSizes = pool_size.data();
			}

			if (vkCreateDescriptorPool(device->get_device(), &info, nullptr, &pool) != VK_SUCCESS)
				LOG("Failed to create descriptor pool.\n");

			VkDescriptorSet sets[VULKAN_NUM_SETS_PER_POOL];
			VkDescriptorSetLayout layouts[VULKAN_NUM_SETS_PER_POOL];
			fill(begin(layouts), end(layouts), set_layout);

			VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			alloc.descriptorPool = pool;
			alloc.descriptorSetCount = VULKAN_NUM_SETS_PER_POOL;
			alloc.pSetLayouts = layouts;

			if (vkAllocateDescriptorSets(device->get_device(), &alloc, sets) != VK_SUCCESS)
				LOG("Failed to allocate descriptor sets.\n");
			pools.push_back(pool);

			for (auto set : sets)
				vacant.push_back(object_pool.allocate(set));
		}

		auto node = vacant.back();

		node->index = index;
		node->hash = hash;
		set_nodes[hash] = node;

		vacant.pop_back();
		rings[index].insert_front(node);
		return { node->set, false };
	}
}

DescriptorSetAllocator::~DescriptorSetAllocator()
{
	if (set_layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(device->get_device(), set_layout, nullptr);

	for (auto &pool : pools)
	{
		vkResetDescriptorPool(device->get_device(), pool, 0);
		vkDestroyDescriptorPool(device->get_device(), pool, nullptr);
	}
}
}