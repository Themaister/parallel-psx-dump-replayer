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
	}

	if (vkCreateDescriptorSetLayout(device->get_device(), &info, nullptr, &set_layout) != VK_SUCCESS)
		LOG("Failed to create descriptor set layout.");
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
