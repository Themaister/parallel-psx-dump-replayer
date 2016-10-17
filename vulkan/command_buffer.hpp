#pragma once

#include "buffer.hpp"
#include "image.hpp"
#include "intrusive.hpp"
#include "vulkan.hpp"

namespace Vulkan
{
class Device;
class CommandBuffer : public IntrusivePtrEnabled<CommandBuffer>
{
public:
	CommandBuffer(Device *device, VkCommandBuffer cmd);
	VkCommandBuffer get_command_buffer()
	{
		return cmd;
	}

	bool swapchain_touched() const
	{
		return uses_swapchain;
	}

	void copy_buffer(const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
	                 VkDeviceSize size);
	void copy_buffer(const Buffer &dst, const Buffer &src);

   void copy_buffer_to_image(const Image &image, const Buffer &buffer,
         VkDeviceSize buffer_offset,
         const VkOffset3D &offset,
         const VkExtent3D &extent,
         unsigned row_length, unsigned slice_height,
         const VkImageSubresourceLayers &subresrouce);

   void copy_image_to_buffer(const Buffer &dst, const Image &src,
         VkDeviceSize buffer_offset,
         const VkOffset3D &offset,
         const VkExtent3D &extent,
         unsigned row_length, unsigned slice_height,
         const VkImageSubresourceLayers &subresrouce);


	void buffer_barrier(const Buffer &buffer, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
	                    VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);

	void image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
	                   VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	                   VkAccessFlags dst_access);
	void image_barrier(const Image &image, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
	                   VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);

private:
	Device *device;
	VkCommandBuffer cmd;
	bool uses_swapchain = false;
};

using CommandBufferHandle = IntrusivePtr<CommandBuffer>;
}
