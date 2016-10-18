#include "descriptor_set.hpp"
#include "device.hpp"

namespace Vulkan
{
DescriptorSetAllocator::DescriptorSetAllocator(Device *device, const DescriptorSetLayout &)
    : device(device)
{
}
DescriptorSetAllocator::~DescriptorSetAllocator()
{
	if (layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(device->get_device(), layout, nullptr);
}
}
