#pragma once

#include "vulkan.hpp"

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

class DescriptorSetAllocator
{
public:
	DescriptorSetAllocator(Device *device, const DescriptorSetLayout &layout);
	~DescriptorSetAllocator();
	void operator=(const DescriptorSetAllocator &) = delete;
	DescriptorSetAllocator(const DescriptorSetAllocator &) = delete;

private:
	Device *device;
	VkDescriptorSetLayout layout;
};
}
