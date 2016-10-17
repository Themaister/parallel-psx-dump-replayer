#include "command_buffer.hpp"
#include "format.hpp"

namespace Vulkan
{
CommandBuffer::CommandBuffer(Device *device, VkCommandBuffer cmd)
    : device(device)
    , cmd(cmd)
{
}

void CommandBuffer::copy_buffer(const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
                                VkDeviceSize size)
{
	const VkBufferCopy region = {
		src_offset, dst_offset, size,
	};
	vkCmdCopyBuffer(cmd, src.get_buffer(), dst.get_buffer(), 1, &region);
}

void CommandBuffer::copy_buffer(const Buffer &dst, const Buffer &src)
{
	VK_ASSERT(dst.get_create_info().size == src.get_create_info().size);
	copy_buffer(dst, 0, src, 0, dst.get_create_info().size);
}

void CommandBuffer::copy_buffer_to_image(const Image &image, const Buffer &src, VkDeviceSize buffer_offset,
                                         const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
                                         unsigned slice_height, const VkImageSubresourceLayers &subresource)
{
	const VkBufferImageCopy region = {
		buffer_offset, row_length, slice_height, subresource, offset, extent,
	};
	vkCmdCopyBufferToImage(cmd, src.get_buffer(), image.get_image(), image.get_layout(), 1, &region);
}

void CommandBuffer::copy_image_to_buffer(const Buffer &buffer, const Image &image, VkDeviceSize buffer_offset,
                                         const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
                                         unsigned slice_height, const VkImageSubresourceLayers &subresource)
{
	const VkBufferImageCopy region = {
		buffer_offset, row_length, slice_height, subresource, offset, extent,
	};
	vkCmdCopyImageToBuffer(cmd, image.get_image(), image.get_layout(), buffer.get_buffer(), 1, &region);
}

void CommandBuffer::barrier(VkPipelineStageFlags src_stages, VkAccessFlags src_access, VkPipelineStageFlags dst_stages,
                            VkAccessFlags dst_access)
{
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void CommandBuffer::buffer_barrier(const Buffer &buffer, VkPipelineStageFlags src_stages, VkAccessFlags src_access,
                                   VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.buffer = buffer.get_buffer();
	barrier.offset = 0;
	barrier.size = buffer.get_create_info().size;

	vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void CommandBuffer::image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
                                  VkPipelineStageFlags src_stages, VkAccessFlags src_access,
                                  VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.image = image.get_image();
	barrier.subresourceRange.aspectMask = format_to_aspect_mask(image.get_create_info().format);
	barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

	vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void CommandBuffer::image_barrier(const Image &image, VkPipelineStageFlags src_stages, VkAccessFlags src_access,
                                  VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	image_barrier(image, image.get_layout(), image.get_layout(), src_stages, src_access, dst_stages, dst_access);
}

void CommandBuffer::blit_image(const Image &dst, const Image &src, const VkOffset3D &dst_offset,
                               const VkOffset3D &dst_extent, const VkOffset3D &src_offset, const VkOffset3D &src_extent,
                               unsigned dst_level, unsigned src_level, unsigned dst_base_layer, unsigned src_base_layer,
                               unsigned num_layers, VkFilter filter)
{
	const auto add_offset = [](const VkOffset3D &a, const VkOffset3D &b) -> VkOffset3D {
		return { a.x + b.x, a.y + b.y, a.z + b.z };
	};

	const VkImageBlit blit = {
		{ format_to_aspect_mask(src.get_create_info().format), src_level, src_base_layer, num_layers },
		{ src_offset, add_offset(src_offset, src_extent) },
		{ format_to_aspect_mask(dst.get_create_info().format), dst_level, dst_base_layer, num_layers },
		{ dst_offset, add_offset(dst_offset, dst_extent) },
	};

	vkCmdBlitImage(cmd, src.get_image(), src.get_layout(), dst.get_image(), dst.get_layout(), 1, &blit, filter);
}
}
