#include "renderer.hpp"
#include "renderer_pipelines.hpp"
#include <cstring>

using namespace Vulkan;
using namespace std;

namespace PSX
{
Renderer::Renderer(Device &device, unsigned scaling, const SaveState *state)
    : device(device)
    , scaling(scaling)
    , allocator(device)
{
	auto info = ImageCreateInfo::render_target(FB_WIDTH, FB_HEIGHT, VK_FORMAT_R32_UINT);
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
	             VK_IMAGE_USAGE_SAMPLED_BIT;

	if (state)
	{
		render_state = state->state;
		atlas.set_texture_offset(render_state.texture_offset_x, render_state.texture_offset_y);
		atlas.set_texture_mode(render_state.texture_mode);
		atlas.set_draw_rect(render_state.draw_rect);
		atlas.set_palette_offset(render_state.palette_offset_x, render_state.palette_offset_y);
		atlas.set_texture_window(render_state.cached_window_rect);
		atlas.write_transfer(Domain::Unscaled, { 0, 0, FB_WIDTH, FB_HEIGHT });
	}

	ImageInitialData initial_vram = {
		state ? state->vram.data() : nullptr, 0, 0,
	};
	framebuffer = device.create_image(info, state ? &initial_vram : nullptr);
	info.width *= scaling;
	info.height *= scaling;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
	             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
	             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	scaled_framebuffer = device.create_image(info);
	info.format = device.get_default_depth_format();
	info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	info.domain = ImageDomain::Transient;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth = device.create_image(info);

	atlas.set_hazard_listener(this);

	init_pipelines();

	ensure_command_buffer();
	cmd->clear_image(*scaled_framebuffer, {});
	cmd->clear_image(*framebuffer, {});
	cmd->full_barrier();

	auto dither_info = ImageCreateInfo::immutable_2d_image(4, 4, VK_FORMAT_R8_UNORM);
	// This lut is biased with 4 to be able to use UNORM easily.
	static const uint8_t dither_lut_data[16] = { 0, 4, 1, 5, 6, 2, 7, 3, 1, 5, 0, 4, 7, 3, 6, 2 };

	ImageInitialData dither_initial = { dither_lut_data };
	dither_lut = device.create_image(dither_info, &dither_initial);

	device.submit(cmd);
	cmd.reset();
	device.flush_frame();
}

Renderer::SaveState Renderer::save_vram_state()
{
	auto buffer =
	    device.create_buffer({ BufferDomain::CachedHost, FB_WIDTH * FB_HEIGHT * sizeof(uint32_t), 0 }, nullptr);
	atlas.read_transfer(Domain::Unscaled, { 0, 0, FB_WIDTH, FB_HEIGHT });
	ensure_command_buffer();
	cmd->copy_image_to_buffer(*buffer, *framebuffer, 0, { 0, 0, 0 }, { FB_WIDTH, FB_HEIGHT, 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
	             VK_ACCESS_HOST_READ_BIT);
	device.submit(cmd);
	cmd.reset();

	device.wait_idle();
	void *ptr = device.map_host_buffer(*buffer, MEMORY_ACCESS_READ);
	std::vector<uint32_t> vram(FB_WIDTH * FB_HEIGHT);
	memcpy(vram.data(), ptr, FB_WIDTH * FB_HEIGHT * sizeof(uint32_t));
	device.unmap_host_buffer(*buffer);
	return { move(vram), render_state };
}

void Renderer::init_pipelines()
{
	switch (scaling)
	{
	case 8:
		pipelines.resolve_to_unscaled = device.create_program(resolve_to_unscaled_8, sizeof(resolve_to_unscaled_8));
		break;

	case 4:
		pipelines.resolve_to_unscaled = device.create_program(resolve_to_unscaled_4, sizeof(resolve_to_unscaled_4));
		break;

	default:
		pipelines.resolve_to_unscaled = device.create_program(resolve_to_unscaled_2, sizeof(resolve_to_unscaled_2));
		break;
	}

	pipelines.scaled_quad_blitter =
	    device.create_program(quad_vert, sizeof(quad_vert), scaled_quad_frag, sizeof(scaled_quad_frag));
	pipelines.bpp24_quad_blitter =
	    device.create_program(quad_vert, sizeof(quad_vert), bpp24_quad_frag, sizeof(bpp24_quad_frag));
	pipelines.unscaled_quad_blitter =
	    device.create_program(quad_vert, sizeof(quad_vert), unscaled_quad_frag, sizeof(unscaled_quad_frag));
	pipelines.copy_to_vram = device.create_program(copy_vram_comp, sizeof(copy_vram_comp));
	pipelines.copy_to_vram_masked = device.create_program(copy_vram_masked_comp, sizeof(copy_vram_masked_comp));
	pipelines.resolve_to_scaled = device.create_program(resolve_to_scaled, sizeof(resolve_to_scaled));
	pipelines.blit_vram_unscaled = device.create_program(blit_vram_unscaled_comp, sizeof(blit_vram_unscaled_comp));
	pipelines.blit_vram_scaled = device.create_program(blit_vram_scaled_comp, sizeof(blit_vram_scaled_comp));
	pipelines.blit_vram_unscaled_masked =
	    device.create_program(blit_vram_unscaled_masked_comp, sizeof(blit_vram_unscaled_masked_comp));
	pipelines.blit_vram_scaled_masked =
	    device.create_program(blit_vram_scaled_masked_comp, sizeof(blit_vram_scaled_masked_comp));
	pipelines.opaque_flat =
	    device.create_program(opaque_flat_vert, sizeof(opaque_flat_vert), opaque_flat_frag, sizeof(opaque_flat_frag));
	pipelines.opaque_textured = device.create_program(opaque_textured_vert, sizeof(opaque_textured_vert),
	                                                  opaque_textured_frag, sizeof(opaque_textured_frag));
	pipelines.opaque_semi_transparent = device.create_program(opaque_textured_vert, sizeof(opaque_textured_vert),
	                                                          opaque_semitrans_frag, sizeof(opaque_semitrans_frag));
	pipelines.semi_transparent = device.create_program(opaque_textured_vert, sizeof(opaque_textured_vert),
	                                                   semitrans_frag, sizeof(semitrans_frag));
	pipelines.semi_transparent_masked_add = device.create_program(opaque_textured_vert, sizeof(opaque_textured_vert),
	                                                              feedback_add_frag, sizeof(feedback_add_frag));
	pipelines.semi_transparent_masked_average = device.create_program(
	    opaque_textured_vert, sizeof(opaque_textured_vert), feedback_avg_frag, sizeof(feedback_avg_frag));
	pipelines.semi_transparent_masked_sub = device.create_program(opaque_textured_vert, sizeof(opaque_textured_vert),
	                                                              feedback_sub_frag, sizeof(feedback_sub_frag));
	pipelines.semi_transparent_masked_add_quarter =
	    device.create_program(opaque_textured_vert, sizeof(opaque_textured_vert), feedback_add_quarter_frag,
	                          sizeof(feedback_add_quarter_frag));

	pipelines.flat_masked_add =
	    device.create_program(opaque_flat_vert, sizeof(opaque_flat_vert), feedback_add_frag, sizeof(feedback_add_frag));
	pipelines.flat_masked_average = device.create_program(opaque_flat_vert, sizeof(opaque_flat_vert),
	                                                      feedback_flat_avg_frag, sizeof(feedback_flat_avg_frag));
	pipelines.flat_masked_sub = device.create_program(opaque_flat_vert, sizeof(opaque_flat_vert),
	                                                  feedback_flat_sub_frag, sizeof(feedback_flat_sub_frag));
	pipelines.flat_masked_add_quarter =
	    device.create_program(opaque_flat_vert, sizeof(opaque_flat_vert), feedback_flat_add_quarter_frag,
	                          sizeof(feedback_flat_add_quarter_frag));
}

void Renderer::set_draw_rect(const Rect &rect)
{
	atlas.set_draw_rect(rect);
	render_state.draw_rect = rect;
}

void Renderer::clear_rect(const Rect &rect, FBColor color)
{
	atlas.clear_rect(rect, color);
}

Rect Renderer::compute_window_rect(const TextureWindow &window)
{
	unsigned mask_bits_x = 32 - leading_zeroes(window.mask_x);
	unsigned mask_bits_y = 32 - leading_zeroes(window.mask_y);
	unsigned x = window.or_x & ~((1u << mask_bits_x) - 1);
	unsigned y = window.or_y & ~((1u << mask_bits_y) - 1);
	return { x, y, (1u << mask_bits_x) - 1, (1u << mask_bits_y) - 1 };
}

void Renderer::set_texture_window(const TextureWindow &window)
{
	render_state.texture_window = window;
	render_state.cached_window_rect = compute_window_rect(window);
}

void Renderer::scanout()
{
	scanout(render_state.display_mode);
	//scanout({ 0, 0, FB_WIDTH, FB_HEIGHT });
}

BufferHandle Renderer::scanout_vram_to_buffer(unsigned &width, unsigned &height)
{
	atlas.read_transfer(Domain::Scaled, { 0, 0, FB_WIDTH, FB_HEIGHT });
	ensure_command_buffer();

	auto buffer =
	    device.create_buffer({ BufferDomain::CachedHost, scaling * scaling * FB_WIDTH * FB_HEIGHT * 4, 0 }, nullptr);
	cmd->copy_image_to_buffer(*buffer, *scaled_framebuffer, 0, { 0, 0, 0 },
	                          { scaling * FB_WIDTH, scaling * FB_HEIGHT, 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
	             VK_ACCESS_HOST_READ_BIT);

	device.submit(cmd);
	cmd.reset();
	device.wait_idle();
	width = FB_WIDTH * scaling;
	height = FB_HEIGHT * scaling;
	return buffer;
}

BufferHandle Renderer::scanout_to_buffer(bool draw_area, unsigned &width, unsigned &height)
{
	auto &rect = draw_area ? render_state.draw_rect : render_state.display_mode;
	if (rect.width == 0 || rect.height == 0 || !render_state.display_on)
		return nullptr;

	atlas.read_transfer(Domain::Scaled, rect);
	ensure_command_buffer();

	auto buffer = device.create_buffer(
	    { BufferDomain::CachedHost, scaling * scaling * rect.width * rect.height * 4, 0 }, nullptr);
	cmd->copy_image_to_buffer(*buffer, *scaled_framebuffer, 0, { int(scaling * rect.x), int(scaling * rect.y), 0 },
	                          { scaling * rect.width, scaling * rect.height, 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
	             VK_ACCESS_HOST_READ_BIT);

	device.submit(cmd);
	cmd.reset();
	device.wait_idle();
	width = scaling * rect.width;
	height = scaling * rect.height;
	return buffer;
}

ImageHandle Renderer::scanout_to_texture(VkFormat format)
{
	auto &rect = render_state.display_mode;

	if (rect.width == 0 || rect.height == 0 || !render_state.display_on)
	{
		// Black screen, just flush out everything.
		atlas.read_fragment(Domain::Scaled, { 0, 0, FB_WIDTH, FB_HEIGHT });

		ensure_command_buffer();

		auto info =
		    ImageCreateInfo::render_target(rect.width ? rect.width : 64u, rect.height ? rect.height : 64u, format);
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		auto image = device.create_image(info);

		RenderPassInfo rp;
		rp.color_attachments[0] = &image->get_view();
		rp.num_color_attachments = 1;
		rp.op_flags = RENDER_PASS_OP_COLOR_OPTIMAL_BIT | RENDER_PASS_OP_CLEAR_COLOR_BIT;

		cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		image->set_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		cmd->begin_render_pass(rp);
		cmd->end_render_pass();

		cmd->image_barrier(*image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		image->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		device.submit(cmd);
		cmd.reset();
		return image;
	}
	else
	{
		if (render_state.bpp24)
		{
			auto tmp = rect;
			tmp.width = (tmp.width * 3 + 1) / 2;
			tmp.width = min(tmp.width, FB_WIDTH - tmp.x);
			atlas.read_fragment(Domain::Unscaled, tmp);
		}
		else
			atlas.read_fragment(Domain::Scaled, rect);

		ensure_command_buffer();

		auto info = ImageCreateInfo::render_target(rect.width * (render_state.bpp24 ? 1 : scaling),
		                                           rect.height * (render_state.bpp24 ? 1 : scaling),
		                                           render_state.bpp24 ? VK_FORMAT_R8G8B8A8_UNORM : format);
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		auto image = device.create_image(info);

		RenderPassInfo rp;
		rp.color_attachments[0] = &image->get_view();
		rp.num_color_attachments = 1;
		rp.op_flags = RENDER_PASS_OP_COLOR_OPTIMAL_BIT;

		cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		image->set_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		cmd->begin_render_pass(rp);
		cmd->set_quad_state();

		if (render_state.bpp24)
		{
			cmd->set_program(*pipelines.bpp24_quad_blitter);
			cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestClamp);
		}
		else
		{
			cmd->set_program(*pipelines.scaled_quad_blitter);
			cmd->set_texture(0, 0, scaled_framebuffer->get_view(), StockSampler::LinearClamp);
		}

		int8_t *data = static_cast<int8_t *>(cmd->allocate_vertex_data(0, 8, 2));
		*data++ = -128;
		*data++ = -128;
		*data++ = +127;
		*data++ = -128;
		*data++ = -128;
		*data++ = +127;
		*data++ = +127;
		*data++ = +127;
		struct Push
		{
			float offset[2];
			float scale[2];
		};
		Push push = { { float(rect.x) / FB_WIDTH, float(rect.y) / FB_HEIGHT },
			          { float(rect.width) / FB_WIDTH, float(rect.height) / FB_HEIGHT } };
		cmd->push_constants(&push, 0, sizeof(push));
		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R8G8_SNORM, 0);
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		counters.draw_calls++;
		counters.vertices += 4;
		cmd->draw(4);
		cmd->end_render_pass();

		cmd->image_barrier(*image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		image->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		device.submit(cmd);
		cmd.reset();
		return image;
	}
}

void Renderer::scanout(const Rect &rect)
{
	if (rect.width == 0 || rect.height == 0 || !render_state.display_on)
	{
		// Black screen, just flush out everything.
		atlas.read_fragment(Domain::Scaled, { 0, 0, FB_WIDTH, FB_HEIGHT });

		ensure_command_buffer();
		auto info = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(info);
		cmd->end_render_pass();
		device.submit(cmd);
		cmd.reset();
		return;
	}

	if (render_state.bpp24)
	{
		auto tmp = rect;
		tmp.width = (tmp.width * 3 + 1) / 2;
		tmp.width = min(tmp.width, FB_WIDTH - tmp.x);
		atlas.read_fragment(Domain::Unscaled, tmp);
	}
	else
		atlas.read_fragment(Domain::Scaled, rect);

	ensure_command_buffer();
	cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
	cmd->set_quad_state();

	if (render_state.bpp24)
	{
		cmd->set_program(*pipelines.bpp24_quad_blitter);
		cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestClamp);
	}
	else
	{
		cmd->set_program(*pipelines.scaled_quad_blitter);
		cmd->set_texture(0, 0, scaled_framebuffer->get_view(), StockSampler::LinearClamp);
	}

	int8_t *data = static_cast<int8_t *>(cmd->allocate_vertex_data(0, 8, 2));
	*data++ = -128;
	*data++ = -128;
	*data++ = +127;
	*data++ = -128;
	*data++ = -128;
	*data++ = +127;
	*data++ = +127;
	*data++ = +127;
	struct Push
	{
		float offset[2];
		float scale[2];
	};
	Push push = { { float(rect.x) / FB_WIDTH, float(rect.y) / FB_HEIGHT },
		          { float(rect.width) / FB_WIDTH, float(rect.height) / FB_HEIGHT } };
	cmd->push_constants(&push, 0, sizeof(push));
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R8G8_SNORM, 0);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	counters.draw_calls++;
	counters.vertices += 4;
	cmd->draw(4);
	cmd->end_render_pass();

	device.submit(cmd);
	cmd.reset();
}

void Renderer::hazard(StatusFlags flags)
{
	VkPipelineStageFlags src_stages = 0;
	VkAccessFlags src_access = 0;
	VkPipelineStageFlags dst_stages = 0;
	VkAccessFlags dst_access = 0;

	if (flags & (STATUS_FRAGMENT_FB_READ | STATUS_FRAGMENT_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
	if (flags & (STATUS_FRAGMENT_FB_WRITE | STATUS_FRAGMENT_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
	}

	if (flags & (STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	if (flags & (STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		src_access |= VK_ACCESS_SHADER_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
		              VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	if (flags & (STATUS_TRANSFER_FB_READ | STATUS_TRANSFER_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	if (flags & (STATUS_TRANSFER_FB_WRITE | STATUS_TRANSFER_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		src_access |= VK_ACCESS_TRANSFER_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
		              VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	// Invalidate render target caches.
	if (flags & (STATUS_TRANSFER_SFB_WRITE | STATUS_COMPUTE_SFB_WRITE | STATUS_FRAGMENT_SFB_WRITE))
	{
		dst_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dst_access |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		              VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	}

	// 24-bpp scanout hazard
	if (flags & STATUS_COMPUTE_FB_WRITE)
	{
		dst_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT;
	}

	dst_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;

	// If we have out-standing jobs in the compute pipe, issue them into cmdbuffer before injecting the barrier.
	if (flags & (STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_READ | STATUS_COMPUTE_SFB_WRITE))
	{
		flush_blits();
		flush_resolves();
	}
	if (flags & (STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_SFB_READ))
		flush_texture_allocator();

	VK_ASSERT(src_stages);
	VK_ASSERT(dst_stages);
	ensure_command_buffer();
	cmd->barrier(src_stages, src_access, dst_stages, dst_access);
}

void Renderer::flush_resolves()
{
	struct Push
	{
		float inv_size[2];
		uint32_t scale;
	};

	if (!queue.scaled_resolves.empty())
	{
		ensure_command_buffer();
		cmd->set_program(*pipelines.resolve_to_scaled);
		cmd->set_storage_texture(0, 0, scaled_framebuffer->get_view());
		cmd->set_texture(0, 1, framebuffer->get_view(), StockSampler::NearestClamp);

		unsigned size = queue.scaled_resolves.size();
		for (unsigned i = 0; i < size; i += 1024)
		{
			unsigned to_run = min(size - i, 1024u);

			Push push = { { 1.0f / (scaling * FB_WIDTH), 1.0f / (scaling * FB_HEIGHT) }, scaling };
			cmd->push_constants(&push, 0, sizeof(push));
			void *ptr = cmd->allocate_constant_data(1, 0, to_run * sizeof(VkRect2D));
			memcpy(ptr, queue.scaled_resolves.data() + i, to_run * sizeof(VkRect2D));
			cmd->dispatch(scaling, scaling, to_run);
		}
	}

	if (!queue.unscaled_resolves.empty())
	{
		ensure_command_buffer();
		cmd->set_program(*pipelines.resolve_to_unscaled);
		cmd->set_storage_texture(0, 0, framebuffer->get_view());
		cmd->set_texture(0, 1, scaled_framebuffer->get_view(), StockSampler::LinearClamp);

		unsigned size = queue.unscaled_resolves.size();
		for (unsigned i = 0; i < size; i += 1024)
		{
			unsigned to_run = min(size - i, 1024u);

			Push push = { { 1.0f / FB_WIDTH, 1.0f / FB_HEIGHT }, 1u };
			cmd->push_constants(&push, 0, sizeof(push));
			void *ptr = cmd->allocate_constant_data(1, 0, to_run * sizeof(VkRect2D));
			memcpy(ptr, queue.unscaled_resolves.data() + i, to_run * sizeof(VkRect2D));
			cmd->dispatch(1, 1, to_run);
		}
	}

	queue.scaled_resolves.clear();
	queue.unscaled_resolves.clear();
}

void Renderer::resolve(Domain target_domain, unsigned x, unsigned y)
{
	if (target_domain == Domain::Scaled)
		queue.scaled_resolves.push_back({ { int(x), int(y) }, { BLOCK_WIDTH, BLOCK_HEIGHT } });
	else
		queue.unscaled_resolves.push_back({ { int(x), int(y) }, { BLOCK_WIDTH, BLOCK_HEIGHT } });
}

void Renderer::ensure_command_buffer()
{
	if (!cmd)
		cmd = device.request_command_buffer();
}

void Renderer::discard_render_pass()
{
	reset_queue();
}

float Renderer::allocate_depth()
{
	atlas.write_fragment();
	primitive_index++;
	return 1.0f - primitive_index * (2.0f / 0xffffff); // Double the epsilon to be safe(r) when w is used.
}

void Renderer::build_attribs(BufferVertex *output, const Vertex *vertices, unsigned count)
{
	unsigned min_u = 256;
	unsigned max_u = 0;
	unsigned min_v = 256;
	unsigned max_v = 0;

	unsigned shift;
	switch (render_state.texture_mode)
	{
	case TextureMode::Palette4bpp:
		shift = 2;
		break;
	case TextureMode::Palette8bpp:
		shift = 1;
		break;
	default:
		shift = 0;
		break;
	}

	// Temporary hack while I figure out how to best solve texturing ...
	if (render_state.texture_mode != TextureMode::None)
	{
		if (render_state.texture_window.mask_x == 0xffu && render_state.texture_window.mask_y == 0xffu)
		{
			// If we're not using texture window, we're likely accessing a small subset of the texture.
			for (unsigned i = 0; i < count; i++)
			{
				min_u = min<unsigned>(min_u, vertices[i].u);
				max_u = max<unsigned>(max_u, vertices[i].u + 1); // Account for bilinear.
				min_v = min<unsigned>(min_v, vertices[i].v);
				max_v = max<unsigned>(max_v, vertices[i].v + 1);
			}

			// We wrap around at page-end for bilinear, clamp the end.
			max_u = min(max_u, 255u);
			max_v = min(max_v, 255u);

			unsigned width = max_u - min_u + 1;
			unsigned height = max_v - min_v + 1;
			width = std::max(width, 8u);
			height = std::max(height, 8u);

#ifdef VRAM_ATLAS
			width = min(width, FB_WIDTH - (render_state.texture_offset_x + (min_u >> shift)));
			height = min(height, FB_HEIGHT - (render_state.texture_offset_y + min_v));
#endif

			atlas.set_texture_window({ min_u, min_v, width, height });
		}
		else
		{
			// If we have a masked texture window, assume this is the true rect we should use.
			auto effective_rect = render_state.cached_window_rect;
			VK_ASSERT(render_state.texture_offset_x + (effective_rect.width >> shift) <= FB_WIDTH);
			atlas.set_texture_window(effective_rect);
		}
	}

	float z = allocate_depth();
	for (unsigned i = 0; i < count; i++)
	{
		output[i] = {
			vertices[i].x + render_state.draw_offset_x,
			vertices[i].y + render_state.draw_offset_y,
			z,
			vertices[i].w,
#ifndef VRAM_ATLAS
			int(vertices[i].u - min_u) * last_uv_scale_x,
			int(vertices[i].v - min_v) * last_uv_scale_y,
			float(last_surface.layer),
#endif
			vertices[i].color & 0xffffffu,
#ifdef VRAM_ATLAS
			render_state.texture_window,
			int16_t(render_state.palette_offset_x),
			int16_t(render_state.palette_offset_y),
			int16_t(shift),
			int8_t(vertices[i].u),
			int8_t(vertices[i].v),
			int8_t(render_state.texture_offset_x / 64u),
			int8_t(render_state.texture_offset_y / 256u),
#endif
		};

		if (render_state.texture_mode != TextureMode::None && !render_state.texture_color_modulate)
			output[i].color = 0x808080;

		output[i].color |= render_state.force_mask_bit ? 0xff000000u : 0u;
	}
}

std::vector<Renderer::BufferVertex> *Renderer::select_pipeline()
{
	// For mask testing, force primitives through the serialized blend path.
	if (render_state.mask_test)
		return nullptr;

	if (render_state.texture_mode != TextureMode::None)
	{
		if (render_state.semi_transparent != SemiTransparentMode::None)
		{
#ifdef VRAM_ATLAS
			return &queue.semi_transparent_opaque;
#else
			if (last_surface.texture >= queue.semi_transparent_opaque.size())
				queue.semi_transparent_opaque.resize(last_surface.texture + 1);
			return &queue.semi_transparent_opaque[last_surface.texture];
#endif
		}
		else
		{
#ifdef VRAM_ATLAS
			return &queue.opaque_textured;
#else
			if (last_surface.texture >= queue.opaque_textured.size())
				queue.opaque_textured.resize(last_surface.texture + 1);
			return &queue.opaque_textured[last_surface.texture];
#endif
		}
	}
	else if (render_state.semi_transparent != SemiTransparentMode::None)
	{
#ifdef VRAM_ATLAS
		return &queue.semi_transparent_opaque;
#else
		if (last_surface.texture >= queue.semi_transparent_opaque.size())
			queue.semi_transparent_opaque.resize(last_surface.texture + 1);
		return &queue.semi_transparent_opaque[last_surface.texture];
#endif
	}
	else
		return &queue.opaque;
}

void Renderer::draw_triangle(const Vertex *vertices)
{
	if (!render_state.draw_rect.width || !render_state.draw_rect.height)
		return;

	counters.native_draw_calls++;

	BufferVertex vert[3];
	build_attribs(vert, vertices, 3);
	auto *out = select_pipeline();
	if (out)
	{
		for (unsigned i = 0; i < 3; i++)
			out->push_back(vert[i]);
	}

	if (render_state.mask_test || render_state.semi_transparent != SemiTransparentMode::None)
	{
		unsigned last_texture = render_state.texture_mode != TextureMode::None ? last_surface.texture : 0;
		for (unsigned i = 0; i < 3; i++)
			queue.semi_transparent.push_back(vert[i]);
		queue.semi_transparent_state.push_back({ last_texture, render_state.semi_transparent,
		                                         render_state.texture_mode != TextureMode::None,
		                                         render_state.mask_test });

		// We've hit the dragon path, we'll need programmable blending for this render pass.
		if (render_state.mask_test && render_state.semi_transparent != SemiTransparentMode::None)
			render_pass_is_feedback = true;
	}
}

void Renderer::draw_quad(const Vertex *vertices)
{
	if (!render_state.draw_rect.width || !render_state.draw_rect.height)
		return;

	counters.native_draw_calls++;

	BufferVertex vert[4];
	build_attribs(vert, vertices, 4);
	auto *out = select_pipeline();
	if (out)
	{
		out->push_back(vert[0]);
		out->push_back(vert[1]);
		out->push_back(vert[2]);
		out->push_back(vert[3]);
		out->push_back(vert[2]);
		out->push_back(vert[1]);
	}

	if (render_state.mask_test || render_state.semi_transparent != SemiTransparentMode::None)
	{
		unsigned last_texture = render_state.texture_mode != TextureMode::None ? last_surface.texture : 0;
		queue.semi_transparent.push_back(vert[0]);
		queue.semi_transparent.push_back(vert[1]);
		queue.semi_transparent.push_back(vert[2]);
		queue.semi_transparent.push_back(vert[3]);
		queue.semi_transparent.push_back(vert[2]);
		queue.semi_transparent.push_back(vert[1]);
		queue.semi_transparent_state.push_back({ last_texture, render_state.semi_transparent,
		                                         render_state.texture_mode != TextureMode::None,
		                                         render_state.mask_test });
		queue.semi_transparent_state.push_back({ last_texture, render_state.semi_transparent,
		                                         render_state.texture_mode != TextureMode::None,
		                                         render_state.mask_test });

		// We've hit the dragon path, we'll need programmable blending for this render pass.
		if (render_state.mask_test && render_state.semi_transparent != SemiTransparentMode::None)
			render_pass_is_feedback = true;
	}
}

void Renderer::clear_quad_separate(const Rect &rect, FBColor color)
{
	ensure_command_buffer();

	RenderPassInfo info = {};
	info.color_attachments[0] = &scaled_framebuffer->get_view();
	info.num_color_attachments = 1;

	info.op_flags = RENDER_PASS_OP_STORE_COLOR_BIT | RENDER_PASS_OP_CLEAR_COLOR_BIT;
	fbcolor_to_rgba32f(info.clear_color[0].float32, color);

	info.render_area.offset = { int(rect.x * scaling), int(rect.y * scaling) };
	info.render_area.extent = { rect.width * scaling, rect.height * scaling };

	counters.render_passes++;
	cmd->begin_render_pass(info);
	cmd->end_render_pass();

	// Render passes are implicitly synchronized.
	cmd->image_barrier(*scaled_framebuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
}

void Renderer::clear_quad(const Rect &rect, FBColor color)
{
	auto old = atlas.set_texture_mode(TextureMode::None);
	float z = allocate_depth();
	atlas.set_texture_mode(old);

#ifdef VRAM_ATLAS
	BufferVertex pos0 = { float(rect.x), float(rect.y), z, 1.0f, fbcolor_to_rgba8(color) };
	BufferVertex pos1 = { float(rect.x) + float(rect.width), float(rect.y), z, 1.0f, fbcolor_to_rgba8(color) };
	BufferVertex pos2 = { float(rect.x), float(rect.y) + float(rect.height), z, 1.0f, fbcolor_to_rgba8(color) };
	BufferVertex pos3 = { float(rect.x) + float(rect.width), float(rect.y) + float(rect.height), z, 1.0f,
		                  fbcolor_to_rgba8(color) };
#else
	BufferVertex pos0 = { float(rect.x), float(rect.y), z, 1.0f, 0.0f, 0.0f, 0.0f, fbcolor_to_rgba8(color) };
	BufferVertex pos1 = {
		float(rect.x) + float(rect.width), float(rect.y), z, 1.0f, 0.0f, 0.0f, 0.0f, fbcolor_to_rgba8(color)
	};
	BufferVertex pos2 = { float(rect.x),          float(rect.y) + float(rect.height), z, 1.0f, 0.0f, 0.0f, 0.0f,
		                  fbcolor_to_rgba8(color) };
	BufferVertex pos3 = { float(rect.x) + float(rect.width),
		                  float(rect.y) + float(rect.height),
		                  z,
		                  1.0f,
		                  0.0f,
		                  0.0f,
		                  0.0f,
		                  fbcolor_to_rgba8(color) };
#endif
	queue.opaque.push_back(pos0);
	queue.opaque.push_back(pos1);
	queue.opaque.push_back(pos2);
	queue.opaque.push_back(pos3);
	queue.opaque.push_back(pos2);
	queue.opaque.push_back(pos1);
}

void Renderer::flush_render_pass(const Rect &rect)
{
	ensure_command_buffer();
	bool is_clear = atlas.render_pass_is_clear();

	RenderPassInfo info = {};
	info.clear_depth_stencil = { 1.0f, 0 };
	info.color_attachments[0] = &scaled_framebuffer->get_view();
	info.depth_stencil = &depth->get_view();
	info.num_color_attachments = 1;

	info.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT | RENDER_PASS_OP_STORE_COLOR_BIT |
	                RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT;

	if (render_pass_is_feedback)
		info.op_flags |= RENDER_PASS_OP_COLOR_FEEDBACK_BIT;

	if (is_clear)
	{
		FBColor color = atlas.render_pass_clear_color();
		fbcolor_to_rgba32f(info.clear_color[0].float32, color);
		info.op_flags |= RENDER_PASS_OP_CLEAR_COLOR_BIT;
	}
	else
		info.op_flags |= RENDER_PASS_OP_LOAD_COLOR_BIT;

	info.render_area.offset = { int(rect.x * scaling), int(rect.y * scaling) };
	info.render_area.extent = { rect.width * scaling, rect.height * scaling };

	flush_texture_allocator();

	counters.render_passes++;
	cmd->begin_render_pass(info);
	cmd->set_scissor(info.render_area);
	cmd->set_texture(0, 2, dither_lut->get_view(), StockSampler::NearestWrap);

	render_opaque_primitives();
	render_opaque_texture_primitives();
	render_semi_transparent_opaque_texture_primitives();
	render_semi_transparent_primitives();

	cmd->end_render_pass();

	// Render passes are implicitly synchronized.
	cmd->image_barrier(*scaled_framebuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
	                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	reset_queue();
}

void Renderer::render_opaque_primitives()
{
	if (queue.opaque.empty())
		return;

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);

	// Render flat-shaded primitives.
	auto *vert = static_cast<BufferVertex *>(
	    cmd->allocate_vertex_data(0, queue.opaque.size() * sizeof(BufferVertex), sizeof(BufferVertex)));
	for (auto i = queue.opaque.size(); i; i--)
		*vert++ = queue.opaque[i - 1];

	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));

	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_program(*pipelines.opaque_flat);
	counters.draw_calls++;
	counters.vertices += queue.opaque.size();
	cmd->draw(queue.opaque.size());
}

void Renderer::render_semi_transparent_primitives()
{
	unsigned prims = queue.semi_transparent_state.size();
	if (!prims)
		return;

	unsigned last_draw_offset = 0;

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_depth_test(true, false);
	cmd->set_blend_enable(true);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
#ifdef VRAM_ATLAS
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	cmd->set_vertex_attrib(3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x));
	cmd->set_vertex_attrib(4, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, u));
#else
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(BufferVertex, u));
#endif

	auto size = queue.semi_transparent.size() * sizeof(BufferVertex);
	void *verts = cmd->allocate_vertex_data(0, size, sizeof(BufferVertex));
	memcpy(verts, queue.semi_transparent.data(), size);

	auto last_state = queue.semi_transparent_state[0];

	const auto set_state = [&](const SemiTransparentState &state) {
#ifndef VRAM_ATLAS
		cmd->set_texture(0, 1, queue.textures[state.image_index]->get_view(), StockSampler::NearestWrap);
#else
		cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestWrap);
#endif

		switch (state.semi_transparent)
		{
		case SemiTransparentMode::None:
		{
			// For opaque primitives which are just masked, we can make use of fixed function blending.
			cmd->set_blend_enable(true);
			cmd->set_program(state.textured ? *pipelines.opaque_textured : *pipelines.opaque_flat);
#ifndef VRAM_ATLAS
			if (state.textured)
				cmd->set_texture(0, 0, queue.textures[state.image_index]->get_view(), StockSampler::LinearWrap);
#endif
			cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			cmd->set_blend_factors(VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
			                       VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_DST_ALPHA);
			break;
		}
		case SemiTransparentMode::Add:
		{
			if (state.masked)
			{
				cmd->set_program(state.textured ? *pipelines.flat_masked_add :
				                                  *pipelines.semi_transparent_masked_add_quarter);
				cmd->pixel_barrier();
				cmd->set_input_attachment(1, 0, scaled_framebuffer->get_view());
				cmd->set_blend_enable(false);
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ONE);
			}
			else
			{
				cmd->set_program(state.textured ? *pipelines.semi_transparent : *pipelines.opaque_flat);
				cmd->set_blend_enable(true);
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ZERO);
			}
			break;
		}
		case SemiTransparentMode::Average:
		{
			if (state.masked)
			{
				cmd->set_program(state.textured ? *pipelines.flat_masked_average :
				                                  *pipelines.semi_transparent_masked_add_quarter);
				cmd->set_input_attachment(0, 0, scaled_framebuffer->get_view());
				cmd->pixel_barrier();
				cmd->set_blend_enable(false);
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ONE);
			}
			else
			{
				static const float rgba[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
				cmd->set_program(state.textured ? *pipelines.semi_transparent : *pipelines.opaque_flat);
				cmd->set_blend_enable(true);
				cmd->set_blend_constants(rgba);
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_CONSTANT_ALPHA, VK_BLEND_FACTOR_ZERO);
			}
			break;
		}
		case SemiTransparentMode::Sub:
		{
			if (state.masked)
			{
				cmd->set_program(state.textured ? *pipelines.flat_masked_sub :
				                                  *pipelines.semi_transparent_masked_add_quarter);
				cmd->set_input_attachment(0, 0, scaled_framebuffer->get_view());
				cmd->pixel_barrier();
				cmd->set_blend_enable(false);
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ONE);
			}
			else
			{
				cmd->set_program(state.textured ? *pipelines.semi_transparent : *pipelines.opaque_flat);
				cmd->set_blend_enable(true);
				cmd->set_blend_op(VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ZERO);
			}
			break;
		}
		case SemiTransparentMode::AddQuarter:
		{
			if (state.masked)
			{
				cmd->set_program(state.textured ? *pipelines.flat_masked_add_quarter :
				                                  *pipelines.semi_transparent_masked_add_quarter);
				cmd->set_input_attachment(0, 0, scaled_framebuffer->get_view());
				cmd->pixel_barrier();
				cmd->set_blend_enable(false);
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ONE);
			}
			else
			{
				static const float rgba[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
				cmd->set_program(state.textured ? *pipelines.semi_transparent : *pipelines.opaque_flat);
				cmd->set_blend_enable(true);
				cmd->set_blend_constants(rgba);
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ZERO);
			}
			break;
		}
		}
	};
	set_state(last_state);

	// These pixels are blended, so we have to render them in-order.
	// Batch up as long as we can.
	for (unsigned i = 1; i < prims; i++)
	{
		// If we need programmable shading, we can't batch as primitives may overlap.
		// We could in theory do some fancy tests here, but probably overkill here.
		if ((last_state.masked && last_state.semi_transparent != SemiTransparentMode::None) ||
		    (last_state != queue.semi_transparent_state[i]))
		{
			unsigned to_draw = i - last_draw_offset;
			counters.draw_calls++;
			counters.vertices += to_draw * 3;
			cmd->draw(to_draw * 3, 1, last_draw_offset * 3, 0);
			last_draw_offset = i;

			last_state = queue.semi_transparent_state[i];
			set_state(last_state);
		}
	}

	unsigned to_draw = prims - last_draw_offset;
	counters.draw_calls++;
	counters.vertices += to_draw * 3;
	cmd->draw(to_draw * 3, 1, last_draw_offset * 3, 0);
}

void Renderer::render_semi_transparent_opaque_texture_primitives()
{
#ifdef VRAM_ATLAS
	const unsigned num_textures = 1;
	if (queue.semi_transparent_opaque.empty())
		return;
#else
	unsigned num_textures = queue.semi_transparent_opaque.size();
#endif

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_program(*pipelines.opaque_semi_transparent);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
#ifdef VRAM_ATLAS
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	cmd->set_vertex_attrib(3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x));
	cmd->set_vertex_attrib(4, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, u));
#else
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(BufferVertex, u));
#endif

	for (unsigned tex = 0; tex < num_textures; tex++)
	{
#ifdef VRAM_ATLAS
		auto &vertices = queue.semi_transparent_opaque;
#else
		auto &vertices = queue.semi_transparent_opaque[tex];
#endif
		if (vertices.empty())
			continue;

		// Render opaque textured primitives.
		auto *vert = static_cast<BufferVertex *>(
		    cmd->allocate_vertex_data(0, vertices.size() * sizeof(BufferVertex), sizeof(BufferVertex)));
		for (auto i = vertices.size(); i; i--)
			*vert++ = vertices[i - 1];

#ifndef VRAM_ATLAS
		cmd->set_texture(0, 0, queue.textures[tex]->get_view(), StockSampler::NearestWrap);
		cmd->set_texture(0, 1, queue.textures[tex]->get_view(), StockSampler::NearestWrap);
#else
		cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestWrap);
#endif
		counters.draw_calls++;
		counters.vertices += vertices.size();
		cmd->draw(vertices.size());
	}
}

void Renderer::render_opaque_texture_primitives()
{
#ifdef VRAM_ATLAS
	const unsigned num_textures = 1;
	if (queue.opaque_textured.empty())
		return;
#else
	unsigned num_textures = queue.opaque_textured.size();
#endif

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_program(*pipelines.opaque_textured);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
#ifdef VRAM_ATLAS
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	cmd->set_vertex_attrib(3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x)); // Pad to support AMD
	cmd->set_vertex_attrib(4, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, u));
#else
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(BufferVertex, u));
#endif

	for (unsigned tex = 0; tex < num_textures; tex++)
	{
#ifdef VRAM_ATLAS
		auto &vertices = queue.opaque_textured;
#else
		auto &vertices = queue.opaque_textured[tex];
#endif
		if (vertices.empty())
			continue;

		// Render opaque textured primitives.
		auto *vert = static_cast<BufferVertex *>(
		    cmd->allocate_vertex_data(0, vertices.size() * sizeof(BufferVertex), sizeof(BufferVertex)));
		for (auto i = vertices.size(); i; i--)
			*vert++ = vertices[i - 1];

#ifndef VRAM_ATLAS
		cmd->set_texture(0, 0, queue.textures[tex]->get_view(), StockSampler::LinearWrap);
		cmd->set_texture(0, 1, queue.textures[tex]->get_view(), StockSampler::NearestWrap);
#else
		cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestWrap);
#endif
		counters.draw_calls++;
		counters.vertices += vertices.size();
		cmd->draw(vertices.size());
	}
}

void Renderer::upload_texture(Domain domain, const Rect &rect, unsigned off_x, unsigned off_y)
{
	if (domain == Domain::Scaled)
	{
		last_surface = allocator.allocate(
		    domain, { scaling * rect.x, scaling * rect.y, scaling * rect.width, scaling * rect.height },
		    scaling * off_x, scaling * off_y, render_state.palette_offset_x, render_state.palette_offset_y);
	}
	else
		last_surface = allocator.allocate(domain, rect, off_x, off_y, render_state.palette_offset_x,
		                                  render_state.palette_offset_y);

	last_surface.texture += queue.textures.size();
	last_uv_scale_x = 1.0f / rect.width;
	last_uv_scale_y = 1.0f / rect.height;

	if (allocator.get_max_layer_count() >= MAX_LAYERS)
		flush_texture_allocator();
}

void Renderer::flush_blits()
{
	ensure_command_buffer();
	const auto blit = [&](const std::vector<BlitInfo> &infos, Program &program, bool scaled) {
		if (infos.empty())
			return;

		cmd->set_program(program);

		if (scaled)
		{
			cmd->set_storage_texture(0, 0, scaled_framebuffer->get_view());
			cmd->set_texture(0, 1, scaled_framebuffer->get_view(), StockSampler::NearestClamp);
		}
		else
		{
			cmd->set_storage_texture(0, 0, framebuffer->get_view());
			cmd->set_texture(0, 1, framebuffer->get_view(), StockSampler::NearestClamp);
		}

		unsigned size = infos.size();
		unsigned scale = scaled ? scaling : 1u;
		for (unsigned i = 0; i < size; i += 512)
		{
			unsigned to_blit = min(size - i, 512u);
			void *ptr = cmd->allocate_constant_data(1, 0, to_blit * sizeof(BlitInfo));
			memcpy(ptr, infos.data() + i, to_blit * sizeof(BlitInfo));
			cmd->dispatch(scale, scale, to_blit);
		}
	};

	blit(queue.scaled_blits, *pipelines.blit_vram_scaled, true);
	blit(queue.scaled_masked_blits, *pipelines.blit_vram_scaled_masked, true);
	blit(queue.unscaled_blits, *pipelines.blit_vram_unscaled, true);
	blit(queue.unscaled_masked_blits, *pipelines.blit_vram_unscaled_masked, true);
	queue.scaled_blits.clear();
	queue.scaled_masked_blits.clear();
	queue.unscaled_blits.clear();
	queue.unscaled_masked_blits.clear();
}

void Renderer::blit_vram(const Rect &dst, const Rect &src)
{
	VK_ASSERT(dst.width == src.width);
	VK_ASSERT(dst.height == src.height);
	auto domain = atlas.blit_vram(dst, src);

	if (domain == Domain::Scaled)
	{
		auto &q = render_state.mask_test ? queue.scaled_masked_blits : queue.scaled_blits;
		unsigned width = dst.width;
		unsigned height = dst.height;
		for (unsigned y = 0; y < height; y += BLOCK_HEIGHT)
			for (unsigned x = 0; x < width; x += BLOCK_WIDTH)
				q.push_back({
				    { (x + src.x) * scaling, (y + src.y) * scaling },
				    { (x + dst.x) * scaling, (y + dst.y) * scaling },
				    { min(BLOCK_WIDTH, width - x) * scaling, min(BLOCK_HEIGHT, height - y) * scaling },
				    { 0, 0 },
				});
	}
	else
	{
		auto &q = render_state.mask_test ? queue.unscaled_masked_blits : queue.unscaled_blits;
		unsigned width = dst.width;
		unsigned height = dst.height;
		for (unsigned y = 0; y < height; y += BLOCK_HEIGHT)
			for (unsigned x = 0; x < width; x += BLOCK_WIDTH)
				q.push_back({
				    { x + src.x, y + src.y },
				    { x + dst.x, y + dst.y },
				    { min(BLOCK_WIDTH, width - x), min(BLOCK_HEIGHT, height - y) },
				    { 0, 0 },
				});
	}
}

void Renderer::copy_cpu_to_vram(const uint16_t *data, const Rect &rect)
{
	atlas.write_compute(Domain::Unscaled, rect);
	VkDeviceSize size = rect.width * rect.height * sizeof(uint16_t);

	// TODO: Chain allocate this.
	auto buffer = device.create_buffer({ BufferDomain::Host, size, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT }, data);

	BufferViewCreateInfo view_info = {};
	view_info.buffer = buffer.get();

	struct Push
	{
		Rect rect;
		uint32_t offset;
	};

	ensure_command_buffer();
	cmd->set_program(render_state.mask_test ? *pipelines.copy_to_vram_masked : *pipelines.copy_to_vram);
	cmd->set_storage_texture(0, 0, framebuffer->get_view());

	// Vulkan minimum limit, for large buffer views, split up the work.
	if (rect.width * rect.height > 0x10000)
	{
		for (unsigned y = 0; y < rect.height; y += BLOCK_HEIGHT)
		{
			unsigned y_size = min(rect.height - y, BLOCK_HEIGHT);
			view_info.offset = y * rect.width * sizeof(uint16_t);
			view_info.range = y_size * rect.width * sizeof(uint16_t);
			view_info.format = VK_FORMAT_R16_UINT;
			auto view = device.create_buffer_view(view_info);

			Rect small_rect = { rect.x, rect.y + y, rect.width, y_size };

			cmd->set_buffer_view(0, 1, *view);
			Push push = { small_rect, 0 };
			cmd->push_constants(&push, 0, sizeof(push));
			cmd->dispatch((small_rect.width + 7) >> 3, (small_rect.height + 7) >> 3, 1);
		}
	}
	else
	{
		view_info.offset = 0;
		view_info.range = size;
		view_info.format = VK_FORMAT_R16_UINT;
		auto view = device.create_buffer_view(view_info);

		cmd->set_buffer_view(0, 1, *view);

		Push push = { rect, 0 };
		cmd->push_constants(&push, 0, sizeof(push));

		// TODO: Batch up work.
		cmd->dispatch((rect.width + 7) >> 3, (rect.height + 7) >> 3, 1);
	}
}

Renderer::~Renderer()
{
	if (cmd)
		device.submit(cmd);
}

void Renderer::flush_texture_allocator()
{
	counters.texture_flushes++;
	ensure_command_buffer();
	allocator.end(cmd.get(), scaled_framebuffer->get_view(), framebuffer->get_view());
	unsigned num_textures = allocator.get_num_textures();
	for (unsigned i = 0; i < num_textures; i++)
		queue.textures.push_back(allocator.get_image(i));

	allocator.begin();
}

void Renderer::reset_queue()
{
	queue.opaque.clear();
	queue.opaque_textured.clear();
	queue.textures.clear();
	queue.semi_transparent.clear();
	queue.semi_transparent_state.clear();
	queue.semi_transparent_opaque.clear();
	allocator.begin();
	primitive_index = 0;
	render_pass_is_feedback = false;
}
}
