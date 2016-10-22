#include "command_buffer.hpp"
#include "device.hpp"
#include "format.hpp"
#include <string.h>

using namespace std;

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
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

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

void CommandBuffer::invalidate_all()
{
	dirty = ~0u;
	dirty_sets = ~0u;
	is_compute = false;
	current_pipeline = VK_NULL_HANDLE;
	current_layout = nullptr;
	memset(current_sets, 0, sizeof(current_sets));
	current_program = nullptr;
}

void CommandBuffer::begin_render_pass(const RenderPassInfo &info)
{
	VK_ASSERT(!framebuffer);
	VK_ASSERT(!render_pass);

	framebuffer = &device->request_framebuffer(info);
	render_pass = &framebuffer->get_render_pass();

	VkRect2D rect = info.render_area;
	rect.offset.x = min(framebuffer->get_width(), uint32_t(rect.offset.x));
	rect.offset.y = min(framebuffer->get_height(), uint32_t(rect.offset.y));
	rect.extent.width = min(framebuffer->get_width() - rect.offset.x, rect.extent.width);
	rect.extent.height = min(framebuffer->get_height() - rect.offset.y, rect.extent.height);

	VkClearValue clear_values[VULKAN_NUM_ATTACHMENTS + 1];
	unsigned num_clear_values = 0;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		if (info.color_attachments[i])
		{
			clear_values[num_clear_values++].color = info.clear_color[i];
			if (info.color_attachments[i]->get_image().is_swapchain_image())
			{
				auto &image = info.color_attachments[i]->get_image();
				uses_swapchain = true;
				VkImageLayout layout = info.op_flags & RENDER_PASS_OP_COLOR_OPTIMAL_BIT ?
				                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL :
				                           VK_IMAGE_LAYOUT_GENERAL;
				image_barrier(image, VK_IMAGE_LAYOUT_UNDEFINED, layout, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				              0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
				image.set_layout(layout);
			}
		}
	}

	if (info.depth_stencil)
		clear_values[num_clear_values++].depthStencil = info.clear_depth_stencil;

	VkRenderPassBeginInfo begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	begin_info.renderPass = render_pass->get_render_pass();
	begin_info.framebuffer = framebuffer->get_framebuffer();
	begin_info.renderArea = rect;
	begin_info.clearValueCount = num_clear_values;
	begin_info.pClearValues = clear_values;

	vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

	viewport = { 0.0f, 0.0f, float(framebuffer->get_width()), float(framebuffer->get_height()), 0.0f, 1.0f };
	scissor = rect;
	invalidate_all();

	render_pass_info = info;
}

void CommandBuffer::end_render_pass(VkPipelineStageFlags color_access_stages, VkAccessFlags color_access,
                                    VkPipelineStageFlags depth_stencil_access_stages,
                                    VkAccessFlags depth_stencil_access)
{
	VK_ASSERT(framebuffer);
	VK_ASSERT(render_pass);

	vkCmdEndRenderPass(cmd);

	framebuffer = nullptr;
	render_pass = nullptr;

	VkPipelineStageFlags dst_stages = 0;

	VkImageMemoryBarrier barriers[VULKAN_NUM_ATTACHMENTS + 1];
	unsigned num_barriers = 0;

	for (unsigned i = 0; i < render_pass_info.num_color_attachments; i++)
	{
		ImageView *view = render_pass_info.color_attachments[i];
		if (view)
		{
			auto &image = view->get_image();
			auto &barrier = barriers[num_barriers++];
			memset(&barrier, 0, sizeof(barrier));
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.image = image.get_image();
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = color_access & image.get_access_flags();
			barrier.oldLayout = image.get_layout();
			barrier.subresourceRange.aspectMask = format_to_aspect_mask(view->get_format());
			barrier.subresourceRange.baseMipLevel = view->get_create_info().base_level;
			barrier.subresourceRange.baseArrayLayer = view->get_create_info().base_layer;
			barrier.subresourceRange.levelCount = view->get_create_info().levels;
			barrier.subresourceRange.layerCount = view->get_create_info().layers;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			dst_stages |= color_access_stages & image.get_stage_flags();

			if (image.is_swapchain_image())
			{
				barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

				// Validation layer seems to want this, but it's not really required.
				barrier.dstAccessMask |= VK_ACCESS_MEMORY_READ_BIT;

				image.set_layout(barrier.newLayout);
			}
			else
				barrier.newLayout = image.get_layout();

			barrier.dstAccessMask &= image_layout_to_possible_access(barrier.newLayout);
		}
	}

	if (render_pass_info.depth_stencil)
	{
		ImageView *view = render_pass_info.depth_stencil;
		auto &barrier = barriers[num_barriers++];
		memset(&barrier, 0, sizeof(barrier));
		auto &image = view->get_image();
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = image.get_image();
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = depth_stencil_access & image.get_access_flags();
		barrier.oldLayout = image.get_layout();
		barrier.newLayout = image.get_layout();
		barrier.subresourceRange.aspectMask = format_to_aspect_mask(view->get_format());
		barrier.subresourceRange.baseMipLevel = view->get_create_info().base_level;
		barrier.subresourceRange.baseArrayLayer = view->get_create_info().base_layer;
		barrier.subresourceRange.levelCount = view->get_create_info().levels;
		barrier.subresourceRange.layerCount = view->get_create_info().layers;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstAccessMask &= image_layout_to_possible_access(barrier.newLayout);

		dst_stages |= depth_stencil_access_stages & image.get_stage_flags();
	}

	if (!dst_stages)
	{
		dst_stages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, dst_stages, 0, 0, nullptr, 0, nullptr, num_barriers,
	                     barriers);
}

void CommandBuffer::flush_render_state()
{
	VK_ASSERT(current_layout);
	VK_ASSERT(current_program);

	// We've invalidated pipeline state, update the VkPipeline.
	if (get_and_clear(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT | COMMAND_BUFFER_DIRTY_PIPELINE_BIT |
	                  COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT))
	{
		Hasher h;
		auto &layout = current_layout->get_resource_layout();
		for_each_bit(layout.attribute_mask, [&](uint32_t bit) {
			h.u32(bit);
			h.u32(attribs[bit].binding);
			h.u32(attribs[bit].format);
			h.u32(attribs[bit].offset);
		});

		h.u64(render_pass->get_cookie());
		h.u64(current_layout->get_cookie());
		h.u64(current_program->get_cookie());
		h.u64(static_state.words);

		auto hash = h.get();
	}
}

void CommandBuffer::set_vertex_attrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset)
{
	VK_ASSERT(attrib < VULKAN_NUM_VERTEX_ATTRIBS);
	VK_ASSERT(framebuffer);

	auto &attr = attribs[attrib];

	if (attr.binding != binding || attr.format != format || attr.offset != offset)
		set_dirty(COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT);

	attr.binding = binding;
	attr.format = format;
	attr.offset = offset;
}

void CommandBuffer::set_vertex_binding(uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
                                       VkVertexInputRate step_rate)
{
	VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);
	VK_ASSERT(framebuffer);

	VkBuffer vkbuffer = buffer.get_buffer();
	if (vbo_buffers[binding] != vkbuffer || vbo_offsets[binding] != offset)
		set_dirty(COMMAND_BUFFER_DIRTY_DYNAMIC_VERTEX_BIT);
	if (vbo_strides[binding] != stride || vbo_input_rates[binding] != step_rate)
		set_dirty(COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT);

	vbo_buffers[binding] = vkbuffer;
	vbo_offsets[binding] = offset;
	vbo_strides[binding] = stride;
	vbo_input_rates[binding] = step_rate;
}

void CommandBuffer::set_viewport(const VkViewport &viewport)
{
	VK_ASSERT(framebuffer);
	this->viewport = viewport;
	set_dirty(COMMAND_BUFFER_DIRTY_VIEWPORT_BIT);
}

void CommandBuffer::set_scissor(const VkRect2D &rect)
{
	VK_ASSERT(framebuffer);
	scissor = rect;
	set_dirty(COMMAND_BUFFER_DIRTY_SCISSOR_BIT);
}

void CommandBuffer::bind_program(const Program &program)
{
	if (current_program == &program)
		return;

	current_program = &program;
	current_pipeline = VK_NULL_HANDLE;

	VK_ASSERT((framebuffer && current_program->get_shader(ShaderStage::Vertex)) ||
	          (!framebuffer && current_program->get_shader(ShaderStage::Compute)));

	set_dirty(COMMAND_BUFFER_DIRTY_PIPELINE_BIT | COMMAND_BUFFER_DYNAMIC_BITS);

	if (program.get_pipeline_layout() != current_layout)
	{
		current_layout = program.get_pipeline_layout();
		set_dirty(COMMAND_BUFFER_DIRTY_PIPELINE_LAYOUT_BIT);
		dirty_sets = ~0u;
	}
}
}
