#include "command_buffer.hpp"
#include "device.hpp"
#include "format.hpp"
#include <string.h>

using namespace std;

namespace Vulkan
{
CommandBuffer::CommandBuffer(Device *device, VkCommandBuffer cmd, VkPipelineCache cache)
    : device(device)
    , cmd(cmd)
    , cache(cache)
{
	begin_compute();
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

void CommandBuffer::begin_context()
{
	dirty = ~0u;
	dirty_sets = ~0u;
	dirty_vbos = ~0u;
	current_pipeline = VK_NULL_HANDLE;
	current_pipeline_layout = VK_NULL_HANDLE;
	current_layout = nullptr;
	current_program = nullptr;
	memset(cookies, 0, sizeof(cookies));
	memset(secondary_cookies, 0, sizeof(secondary_cookies));
	memset(&index, 0, sizeof(index));
}

void CommandBuffer::begin_compute()
{
	is_compute = true;
	begin_context();
}

void CommandBuffer::begin_graphics()
{
	is_compute = false;
	begin_context();
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
	begin_graphics();

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
		dst_stages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, dst_stages, 0, 0, nullptr, 0, nullptr, num_barriers,
	                     barriers);

	begin_compute();
}

VkPipeline CommandBuffer::build_graphics_pipeline(Hash hash)
{
	// Viewport state
	VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	vp.viewportCount = 1;
	vp.scissorCount = 1;

	// Dynamic state
	VkPipelineDynamicStateCreateInfo dyn = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dyn.dynamicStateCount = 2;
	static const VkDynamicState states[] = {
		VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT,
	};
	dyn.pDynamicStates = states;

	// Blend state
	VkPipelineColorBlendAttachmentState blend_attachments[VULKAN_NUM_ATTACHMENTS];
	VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	blend.attachmentCount = render_pass_info.num_color_attachments;
	blend.pAttachments = blend_attachments;
	for (unsigned i = 0; i < blend.attachmentCount; i++)
	{
		auto &att = blend_attachments[i];
		att = {};
		att.colorWriteMask =
		    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		att.blendEnable = false;
	}

	// Depth state
	VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	ds.stencilTestEnable = false;
	ds.depthTestEnable = false;
	ds.depthWriteEnable = false;

	// Vertex input
	VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkVertexInputAttributeDescription vi_attribs[VULKAN_NUM_VERTEX_ATTRIBS];
	vi.pVertexAttributeDescriptions = vi_attribs;
	uint32_t attr_mask = current_layout->get_resource_layout().attribute_mask;
	uint32_t binding_mask = 0;
	for_each_bit(attr_mask, [&](uint32_t bit) {
		auto &attr = vi_attribs[vi.vertexAttributeDescriptionCount++];
		attr.location = bit;
		attr.binding = attribs[bit].binding;
		attr.format = attribs[bit].format;
		attr.offset = attribs[bit].offset;
		binding_mask |= 1u << attr.binding;
	});

	VkVertexInputBindingDescription vi_bindings[VULKAN_NUM_VERTEX_BUFFERS];
	vi.pVertexBindingDescriptions = vi_bindings;
	for_each_bit(binding_mask, [&](uint32_t bit) {
		auto &bind = vi_bindings[vi.vertexBindingDescriptionCount++];
		bind.binding = bit;
		bind.inputRate = vbo_input_rates[bit];
		bind.stride = vbo_strides[bit];
	});

	// Input assembly
	VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	ia.primitiveRestartEnable = false;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// Multisample
	VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	// TODO: Support more
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Raster
	VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	raster.polygonMode = VK_POLYGON_MODE_FILL;

	// Stages
	VkPipelineShaderStageCreateInfo stages[static_cast<unsigned>(ShaderStage::Count)];
	unsigned num_stages = 0;

	for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
	{
		auto stage = static_cast<ShaderStage>(i);
		if (current_program->get_shader(stage))
		{
			auto &s = stages[num_stages++];
			s = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			s.module = current_program->get_shader(stage)->get_module();
			s.pName = "main";
			s.stage = static_cast<VkShaderStageFlagBits>(1u << i);
		}
	}

	VkGraphicsPipelineCreateInfo pipe = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipe.layout = current_pipeline_layout;
	pipe.renderPass = render_pass->get_render_pass();
	pipe.subpass = 0;

	pipe.pViewportState = &vp;
	pipe.pDynamicState = &dyn;
	pipe.pColorBlendState = &blend;
	pipe.pDepthStencilState = &ds;
	pipe.pVertexInputState = &vi;
	pipe.pInputAssemblyState = &ia;
	pipe.pMultisampleState = &ms;
	pipe.pRasterizationState = &raster;
	pipe.pStages = stages;
	pipe.stageCount = num_stages;

	VkResult res = vkCreateGraphicsPipelines(device->get_device(), cache, 1, &pipe, nullptr, &current_pipeline);
	if (res != VK_SUCCESS)
		LOG("Failed to create graphics pipeline!\n");

	current_program->add_pipeline(hash, current_pipeline);
	return current_pipeline;
}

void CommandBuffer::flush_graphics_pipeline()
{
	Hasher h;
	active_vbos = 0;
	auto &layout = current_layout->get_resource_layout();
	for_each_bit(layout.attribute_mask, [&](uint32_t bit) {
		h.u32(bit);
		active_vbos |= 1u << attribs[bit].binding;
		h.u32(attribs[bit].binding);
		h.u32(attribs[bit].format);
		h.u32(attribs[bit].offset);
	});

	for_each_bit(active_vbos, [&](uint32_t bit) {
		h.u32(vbo_input_rates[bit]);
		h.u32(vbo_strides[bit]);
	});

	h.u64(render_pass->get_cookie());
	h.u64(current_program->get_cookie());
	h.u64(static_state.words);

	auto hash = h.get();
	current_pipeline = current_program->get_pipeline(hash);
	if (current_pipeline == VK_NULL_HANDLE)
		current_pipeline = build_graphics_pipeline(hash);
}

void CommandBuffer::flush_render_state()
{
	VK_ASSERT(current_layout);
	VK_ASSERT(current_program);

	// We've invalidated pipeline state, update the VkPipeline.
	if (get_and_clear(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT | COMMAND_BUFFER_DIRTY_PIPELINE_BIT |
	                  COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT))
	{
		VkPipeline old_pipe = current_pipeline;
		flush_graphics_pipeline();
		if (old_pipe != current_pipeline)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, current_pipeline);
			set_dirty(COMMAND_BUFFER_DYNAMIC_BITS);
		}
	}

	flush_descriptor_sets();

	if (get_and_clear(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT))
	{
		for (auto &range : current_layout->get_resource_layout().ranges)
		{
			if (!range.size)
				continue;

			vkCmdPushConstants(cmd, current_pipeline_layout, range.stageFlags, range.offset, range.size,
			                   push_constant_data + range.offset);
		}
	}

	if (get_and_clear(COMMAND_BUFFER_DIRTY_VIEWPORT_BIT))
		vkCmdSetViewport(cmd, 0, 1, &viewport);
	if (get_and_clear(COMMAND_BUFFER_DIRTY_SCISSOR_BIT))
		vkCmdSetScissor(cmd, 0, 1, &scissor);

	uint32_t update_vbo_mask = dirty_vbos & active_vbos;
	for_each_bit_range(update_vbo_mask, [&](uint32_t binding, uint32_t binding_count) {
#ifdef VULKAN_DEBUG
		for (unsigned i = binding; i < binding + binding_count; i++)
			VK_ASSERT(vbo_buffers[i] != VK_NULL_HANDLE);
#endif
		vkCmdBindVertexBuffers(cmd, binding, binding_count, vbo_buffers + binding, vbo_offsets + binding);
	});
	dirty_vbos &= ~update_vbo_mask;
}

void CommandBuffer::set_vertex_attrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset)
{
	VK_ASSERT(attrib < VULKAN_NUM_VERTEX_ATTRIBS);
	VK_ASSERT(framebuffer);

	auto &attr = attribs[attrib];

	if (attr.binding != binding || attr.format != format || attr.offset != offset)
		set_dirty(COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT);

	VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);

	attr.binding = binding;
	attr.format = format;
	attr.offset = offset;
}

void CommandBuffer::bind_index_buffer(const Buffer &buffer, VkDeviceSize offset, VkIndexType index_type)
{
	if (index.buffer == buffer.get_buffer() && index.offset == offset && index.index_type == index_type)
		return;

	index.buffer = buffer.get_buffer();
	index.offset = offset;
	index.index_type = index_type;
	vkCmdBindIndexBuffer(cmd, buffer.get_buffer(), offset, index_type);
}

void CommandBuffer::set_vertex_binding(uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
                                       VkVertexInputRate step_rate)
{
	VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);
	VK_ASSERT(framebuffer);

	VkBuffer vkbuffer = buffer.get_buffer();
	if (vbo_buffers[binding] != vkbuffer || vbo_offsets[binding] != offset)
		dirty_vbos |= 1u << binding;
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

void CommandBuffer::push_constants(const void *data, VkDeviceSize offset, VkDeviceSize range)
{
	VK_ASSERT(offset + range <= VULKAN_PUSH_CONSTANT_SIZE);
	memcpy(push_constant_data + offset, data, range);
	set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);
}

void CommandBuffer::bind_program(Program &program)
{
	if (current_program && current_program->get_cookie() == program.get_cookie())
		return;

	current_program = &program;
	current_pipeline = VK_NULL_HANDLE;

	VK_ASSERT((framebuffer && current_program->get_shader(ShaderStage::Vertex)) ||
	          (!framebuffer && current_program->get_shader(ShaderStage::Compute)));

	set_dirty(COMMAND_BUFFER_DIRTY_PIPELINE_BIT | COMMAND_BUFFER_DYNAMIC_BITS);

	if (!current_layout)
	{
		dirty_sets = ~0u;
		set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);

		current_layout = program.get_pipeline_layout();
		current_pipeline_layout = current_layout->get_layout();
	}
	else if (program.get_pipeline_layout()->get_cookie() != current_layout->get_cookie())
	{
		auto &new_layout = program.get_pipeline_layout()->get_resource_layout();
		auto &old_layout = current_layout->get_resource_layout();

		// If the push constant layout changes, all descriptor sets
		// are invalidated.
		if (new_layout.push_constant_layout_hash != old_layout.push_constant_layout_hash)
		{
			dirty_sets = ~0u;
			set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);
		}
		else
		{
			// Find the first set whose descriptor set layout differs.
			auto *new_pipe_layout = program.get_pipeline_layout();
			for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
			{
				if (new_pipe_layout->get_allocator(set) != current_layout->get_allocator(set))
				{
					dirty_sets |= ~((1u << set) - 1);
					break;
				}
			}
		}
		current_layout = program.get_pipeline_layout();
		current_pipeline_layout = current_layout->get_layout();
	}
}

void CommandBuffer::set_texture(unsigned set, unsigned binding, const ImageView &view)
{
	if (view.get_cookie() == cookies[set][binding])
		return;

	dirty_sets |= 1u << set;
}

void CommandBuffer::flush_descriptor_set(uint32_t set)
{
	auto &layout = current_layout->get_resource_layout();
	auto &set_layout = layout.sets[set];
	Hash hash = 0;
	uint32_t num_dynamic_offsets = 0;
	uint32_t dynamic_offsets[VULKAN_NUM_BINDINGS];

	Hasher h;

	// UBOs
	for_each_bit(set_layout.uniform_buffer_mask, [&](uint32_t binding) {
		h.u64(cookies[set][binding]);
		h.u32(bindings[set][binding].buffer.range);
		VK_ASSERT(bindings[set][binding].buffer.buffer != VK_NULL_HANDLE);

		dynamic_offsets[num_dynamic_offsets++] = bindings[set][binding].buffer.offset;
	});

	// SSBOs
	for_each_bit(set_layout.storage_buffer_mask, [&](uint32_t binding) {
		h.u64(cookies[set][binding]);
		h.u32(bindings[set][binding].buffer.offset);
		h.u32(bindings[set][binding].buffer.range);
		VK_ASSERT(bindings[set][binding].buffer.buffer != VK_NULL_HANDLE);
	});

	// Sampled images
	for_each_bit(set_layout.sampled_image_mask, [&](uint32_t binding) {
		h.u64(cookies[set][binding]);
		h.u64(secondary_cookies[set][binding]);
		VK_ASSERT(bindings[set][binding].image.imageView != VK_NULL_HANDLE);
		VK_ASSERT(bindings[set][binding].image.sampler != VK_NULL_HANDLE);
	});

	// Storage images
	for_each_bit(set_layout.storage_image_mask, [&](uint32_t binding) {
		h.u64(cookies[set][binding]);
		VK_ASSERT(bindings[set][binding].image.imageView != VK_NULL_HANDLE);
	});

	auto allocated = current_layout->get_allocator(set)->find(hash);

	// The descriptor set was not successfully cached, rebuild.
	if (!allocated.second)
	{
		uint32_t write_count = 0;
		uint32_t buffer_info_count = 0;
		VkWriteDescriptorSet writes[VULKAN_NUM_BINDINGS];
		VkDescriptorBufferInfo buffer_info[VULKAN_NUM_BINDINGS];

		for_each_bit(set_layout.uniform_buffer_mask, [&](uint32_t binding) {
			auto &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;

			// Offsets are applied dynamically.
			auto &buffer = buffer_info[buffer_info_count++];
			buffer = bindings[set][binding].buffer;
			buffer.offset = 0;
			write.pBufferInfo = &buffer;
		});

		for_each_bit(set_layout.storage_buffer_mask, [&](uint32_t binding) {
			auto &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			write.pBufferInfo = &bindings[set][binding].buffer;
		});

		for_each_bit(set_layout.sampled_image_mask, [&](uint32_t binding) {
			auto &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			write.pImageInfo = &bindings[set][binding].image;
		});

		for_each_bit(set_layout.storage_image_mask, [&](uint32_t binding) {
			auto &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			write.pImageInfo = &bindings[set][binding].image;
		});

		vkUpdateDescriptorSets(device->get_device(), write_count, writes, 0, nullptr);
	}

	vkCmdBindDescriptorSets(cmd, render_pass ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE,
	                        current_pipeline_layout, set, 1, &allocated.first, num_dynamic_offsets, dynamic_offsets);
}

void CommandBuffer::flush_descriptor_sets()
{
	auto &layout = current_layout->get_resource_layout();
	uint32_t set_update = layout.descriptor_set_mask & dirty_sets;
	for_each_bit(set_update, [&](uint32_t set) { flush_descriptor_set(set); });
	dirty_sets &= ~set_update;
}

void CommandBuffer::draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	VK_ASSERT(current_program);
	VK_ASSERT(!is_compute);
	flush_render_state();
	vkCmdDraw(cmd, vertex_count, instance_count, first_vertex, first_instance);
}

void CommandBuffer::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                                 int32_t vertex_offset, uint32_t first_instance)
{
	VK_ASSERT(current_program);
	VK_ASSERT(!is_compute);
	VK_ASSERT(index.buffer != VK_NULL_HANDLE);
	flush_render_state();
	vkCmdDrawIndexed(cmd, index_count, instance_count, first_index, vertex_offset, first_instance);
}
}
