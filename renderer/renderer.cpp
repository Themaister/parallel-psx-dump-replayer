#include "renderer.hpp"
#include <cstring>

using namespace Vulkan;
using namespace std;

namespace PSX
{
Renderer::Renderer(Device &device, unsigned scaling)
	: device(device), scaling(scaling), allocator(device)
{
	auto info = ImageCreateInfo::render_target(FB_WIDTH, FB_HEIGHT, VK_FORMAT_R32_UINT);
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
	             VK_IMAGE_USAGE_SAMPLED_BIT;

	framebuffer = device.create_image(info);
	info.width *= scaling;
	info.height *= scaling;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
	             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	scaled_framebuffer = device.create_image(info);
	info.format = VK_FORMAT_D16_UNORM;
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
	device.submit(cmd);
	cmd.reset();
}

void Renderer::init_pipelines()
{
	static const uint32_t quad_vert[] =
#include "quad.vert.inc"
	;
	static const uint32_t scaled_quad_frag[] =
#include "scaled.quad.frag.inc"
	;
	static const uint32_t unscaled_quad_frag[] =
#include "unscaled.quad.frag.inc"
	;
	static const uint32_t copy_vram_comp[] =
#include "copy_vram.comp.inc"
	;
	static const uint32_t resolve_to_scaled[] =
#include "resolve.scaled.comp.inc"
	;
	static const uint32_t resolve_to_unscaled[] =
#include "resolve.unscaled.comp.inc"
	;
	static const uint32_t opaque_flat_vert[] =
#include "opaque.flat.vert.inc"
	;
	static const uint32_t opaque_flat_frag[] =
#include "opaque.flat.frag.inc"
	;
	static const uint32_t opaque_textured_vert[] =
#include "opaque.textured.vert.inc"
	;
	static const uint32_t opaque_textured_frag[] =
#include "opaque.textured.frag.inc"
	;
	static const uint32_t blit_vram_unscaled_comp[] =
#include "blit_vram.unscaled.comp.inc"
	;
	static const uint32_t blit_vram_scaled_comp[] =
#include "blit_vram.scaled.comp.inc"
	;

	pipelines.scaled_quad_blitter =
		device.create_program(quad_vert, sizeof(quad_vert), scaled_quad_frag, sizeof(scaled_quad_frag));
	pipelines.unscaled_quad_blitter =
		device.create_program(quad_vert, sizeof(quad_vert), unscaled_quad_frag, sizeof(unscaled_quad_frag));
	pipelines.copy_to_vram = device.create_program(copy_vram_comp, sizeof(copy_vram_comp));
	pipelines.resolve_to_scaled = device.create_program(resolve_to_scaled, sizeof(resolve_to_scaled));
	pipelines.resolve_to_unscaled = device.create_program(resolve_to_unscaled, sizeof(resolve_to_unscaled));
	pipelines.blit_vram_unscaled = device.create_program(blit_vram_unscaled_comp, sizeof(blit_vram_unscaled_comp));
	pipelines.blit_vram_scaled = device.create_program(blit_vram_scaled_comp, sizeof(blit_vram_scaled_comp));
	pipelines.opaque_flat =
		device.create_program(opaque_flat_vert, sizeof(opaque_flat_vert), opaque_flat_frag, sizeof(opaque_flat_frag));
	pipelines.opaque_textured =
		device.create_program(opaque_textured_vert, sizeof(opaque_textured_vert), opaque_textured_frag, sizeof(opaque_textured_frag));
}

void Renderer::set_draw_rect(const Rect &rect)
{
	atlas.set_draw_rect(rect);
}

void Renderer::clear_rect(const Rect &rect, FBColor color)
{
	atlas.clear_rect(rect, color);
}

void Renderer::set_texture_window(const Rect &rect)
{
	atlas.set_texture_window(rect);
}

void Renderer::scanout(const Rect &rect)
{
	atlas.read_fragment(Domain::Scaled, rect);

	ensure_command_buffer();
	cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
	cmd->set_quad_state();
	cmd->set_texture(0, 0, scaled_framebuffer->get_view(), StockSampler::LinearClamp);
	cmd->set_program(*pipelines.scaled_quad_blitter);
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
	Push push = {{float(rect.x) / FB_WIDTH,     float(rect.y) / FB_HEIGHT},
	             {float(rect.width) / FB_WIDTH, float(rect.height) / FB_HEIGHT}};
	cmd->push_constants(&push, 0, sizeof(push));
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R8G8_SNORM, 0);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
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

	VK_ASSERT((flags & (STATUS_TRANSFER_FB_READ | STATUS_TRANSFER_FB_WRITE | STATUS_TRANSFER_SFB_READ |
	                    STATUS_TRANSFER_SFB_WRITE)) == 0);

	if (flags & (STATUS_FRAGMENT_FB_READ | STATUS_FRAGMENT_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
	if (flags & (STATUS_FRAGMENT_FB_WRITE | STATUS_FRAGMENT_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	}

	if (flags & (STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	if (flags & (STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		src_access |= VK_ACCESS_SHADER_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	}

	// Invalidate render target caches.
	if (flags & (STATUS_COMPUTE_SFB_WRITE | STATUS_FRAGMENT_SFB_WRITE))
	{
		dst_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dst_access |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		              VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	}

	dst_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

	LOG("Hazard!\n");

	VK_ASSERT(src_stages);
	VK_ASSERT(dst_stages);
	ensure_command_buffer();
	cmd->barrier(src_stages, src_access, dst_stages, dst_access);
}

void Renderer::resolve(Domain target_domain, const Rect &rect)
{
	ensure_command_buffer();

	struct Push
	{
		Rect rect;
		float inv_size[2];
		uint32_t scale;
	};

	if (target_domain == Domain::Scaled)
	{
		cmd->set_program(*pipelines.resolve_to_scaled);
		cmd->set_storage_texture(0, 0, scaled_framebuffer->get_view());
		cmd->set_texture(0, 1, framebuffer->get_view(), StockSampler::NearestClamp);

		Push push = {rect, {1.0f / (scaling * FB_WIDTH), 1.0f / (scaling * FB_HEIGHT)}, scaling};
		cmd->push_constants(&push, 0, sizeof(push));
		cmd->dispatch(scaling * (rect.width >> 3), scaling * (rect.height >> 3), 1);
	}
	else
	{
		cmd->set_program(*pipelines.resolve_to_unscaled);
		cmd->set_storage_texture(0, 0, framebuffer->get_view());
		cmd->set_texture(0, 1, scaled_framebuffer->get_view(), StockSampler::LinearClamp);

		Push push = {rect, {1.0f / FB_WIDTH, 1.0f / FB_HEIGHT}, scaling};
		cmd->push_constants(&push, 0, sizeof(push));
		cmd->dispatch(rect.width >> 3, rect.height >> 3, 1);
	}
}

void Renderer::ensure_command_buffer()
{
	if (!cmd)
		cmd = device.request_command_buffer();
}

void Renderer::discard_render_pass()
{
	queue.opaque_position.clear();
	queue.opaque_attrib.clear();
}

float Renderer::allocate_depth()
{
	atlas.write_fragment();
	primitive_index++;
	return 1.0f - primitive_index * (1.0f / 0xffff);
}

void Renderer::draw_triangle(const Vertex *vertices)
{
	float z = allocate_depth();
	BufferPosition pos[3];
	BufferAttrib attr[3];
	for (unsigned i = 0; i < 3; i++)
	{
		pos[i] = {vertices[i].x + render_state.draw_offset_x, vertices[i].y + render_state.draw_offset_y, z,
		          vertices[i].w};
		attr[i] = {0.0f, 0.0f, 0.0f, vertices[i].color};
	}

	vector<BufferPosition> *positions;
	vector<BufferAttrib> *attribs;
	if (texture_mode != TextureMode::None)
	{
		for (unsigned i = 0; i < 3; i++)
		{
			attr[i].u = vertices[i].u * last_uv_scale_x;
			attr[i].v = vertices[i].v * last_uv_scale_y;
			attr[i].layer = float(last_surface.layer);
		}

		if (last_surface.texture >= queue.opaque_textured_attrib.size())
		{
			queue.opaque_textured_position.resize(last_surface.texture + 1);
			queue.opaque_textured_attrib.resize(last_surface.texture + 1);
		}

		positions = &queue.opaque_textured_position[last_surface.texture];
		attribs = &queue.opaque_textured_attrib[last_surface.texture];
	}
	else
	{
		positions = &queue.opaque_position;
		attribs = &queue.opaque_attrib;
	}

	positions->push_back(pos[0]);
	positions->push_back(pos[1]);
	positions->push_back(pos[2]);
	attribs->push_back(attr[0]);
	attribs->push_back(attr[1]);
	attribs->push_back(attr[2]);
}

void Renderer::draw_quad(const Vertex *vertices)
{
	float z = allocate_depth();
	BufferPosition pos[4];
	BufferAttrib attr[4];
	for (unsigned i = 0; i < 4; i++)
	{
		pos[i] = {vertices[i].x + render_state.draw_offset_x, vertices[i].y + render_state.draw_offset_y, z,
		          vertices[i].w};
		attr[i] = {0.0f, 0.0f, 0.0f, vertices[i].color};
	}

	vector<BufferPosition> *positions;
	vector<BufferAttrib> *attribs;
	if (texture_mode != TextureMode::None)
	{
		for (unsigned i = 0; i < 4; i++)
		{
			attr[i].u = vertices[i].u * last_uv_scale_x;
			attr[i].v = vertices[i].v * last_uv_scale_y;
			attr[i].layer = float(last_surface.layer);
		}

		if (last_surface.texture >= queue.opaque_textured_attrib.size())
		{
			queue.opaque_textured_position.resize(last_surface.texture + 1);
			queue.opaque_textured_attrib.resize(last_surface.texture + 1);
		}

		positions = &queue.opaque_textured_position[last_surface.texture];
		attribs = &queue.opaque_textured_attrib[last_surface.texture];
	}
	else
	{
		positions = &queue.opaque_position;
		attribs = &queue.opaque_attrib;
	}

	positions->push_back(pos[0]);
	positions->push_back(pos[1]);
	positions->push_back(pos[2]);
	positions->push_back(pos[3]);
	positions->push_back(pos[2]);
	positions->push_back(pos[1]);
	attribs->push_back(attr[0]);
	attribs->push_back(attr[1]);
	attribs->push_back(attr[2]);
	attribs->push_back(attr[3]);
	attribs->push_back(attr[2]);
	attribs->push_back(attr[1]);
}

void Renderer::clear_quad(const Rect &rect, FBColor color)
{
	auto old = atlas.set_texture_mode(TextureMode::None);
	float z = allocate_depth();
	atlas.set_texture_mode(old);

	BufferPosition pos0 = {float(rect.x), float(rect.y), z, 1.0f};
	BufferPosition pos1 = {float(rect.x) + float(rect.width), float(rect.y), z, 1.0f};
	BufferPosition pos2 = {float(rect.x), float(rect.y) + float(rect.height), z, 1.0f};
	BufferPosition pos3 = {float(rect.x) + float(rect.width), float(rect.y) + float(rect.height), z, 1.0f};
	queue.opaque_position.push_back(pos0);
	queue.opaque_position.push_back(pos1);
	queue.opaque_position.push_back(pos2);
	queue.opaque_position.push_back(pos3);
	queue.opaque_position.push_back(pos2);
	queue.opaque_position.push_back(pos1);
	BufferAttrib attr = {0.0f, 0.0f, 0.0f, fbcolor_to_rgba8(color)};
	for (unsigned i = 0; i < 6; i++)
		queue.opaque_attrib.push_back(attr);
}

void Renderer::flush_render_pass(const Rect &rect)
{
	primitive_index = 0;
	ensure_command_buffer();
	bool is_clear = atlas.render_pass_is_clear();

	RenderPassInfo info = {};
	info.clear_depth_stencil = {1.0f, 0};
	info.color_attachments[0] = &scaled_framebuffer->get_view();
	info.depth_stencil = &depth->get_view();
	info.num_color_attachments = 1;

	info.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT | RENDER_PASS_OP_STORE_COLOR_BIT |
	                RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT;
	if (is_clear)
	{
		FBColor color = atlas.render_pass_clear_color();
		fbcolor_to_rgba32f(info.clear_color[0].float32, color);
		info.op_flags |= RENDER_PASS_OP_CLEAR_COLOR_BIT;
	}
	else
		info.op_flags |= RENDER_PASS_OP_LOAD_COLOR_BIT;

	info.render_area.offset = {int(rect.x * scaling), int(rect.y * scaling)};
	info.render_area.extent = {rect.width * scaling, rect.height * scaling};

	flush_texture_allocator();

	cmd->begin_render_pass(info);
	cmd->set_scissor(info.render_area);

	render_opaque_primitives();
	render_opaque_texture_primitives();

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
	if (queue.opaque_position.empty())
		return;

	VK_ASSERT(queue.opaque_attrib.size() == queue.opaque_position.size());

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);

	// Render flat-shaded primitives.
	auto *pos = static_cast<BufferPosition *>(
		cmd->allocate_vertex_data(0, queue.opaque_position.size() * sizeof(BufferPosition), sizeof(BufferPosition)));
	for (auto i = queue.opaque_position.size(); i; i--)
		*pos++ = queue.opaque_position[i - 1];

	auto *attr = static_cast<BufferAttrib *>(
		cmd->allocate_vertex_data(1, queue.opaque_attrib.size() * sizeof(BufferAttrib), sizeof(BufferAttrib)));
	for (auto i = queue.opaque_attrib.size(); i; i--)
		*attr++ = queue.opaque_attrib[i - 1];

	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferAttrib, color));

	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_program(*pipelines.opaque_flat);
	cmd->draw(queue.opaque_position.size());
}

void Renderer::render_opaque_texture_primitives()
{
	unsigned num_textures = queue.opaque_textured_position.size();

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_program(*pipelines.opaque_textured);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferAttrib, color));
	cmd->set_vertex_attrib(2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(BufferAttrib, u));

	for (unsigned tex = 0; tex < num_textures; tex++)
	{
		auto &position = queue.opaque_textured_position[tex];
		auto &attribs = queue.opaque_textured_attrib[tex];
		if (position.empty())
			continue;

		// Render opaque textured primitives.
		auto *pos = static_cast<BufferPosition *>(
			cmd->allocate_vertex_data(0, position.size() * sizeof(BufferPosition), sizeof(BufferPosition)));
		for (auto i = position.size(); i; i--)
			*pos++ = position[i - 1];

		auto *attr = static_cast<BufferAttrib *>(
			cmd->allocate_vertex_data(1, attribs.size() * sizeof(BufferAttrib), sizeof(BufferAttrib)));
		for (auto i = attribs.size(); i; i--)
			*attr++ = attribs[i - 1];

		cmd->set_texture(0, 0, queue.textures[tex]->get_view(), StockSampler::LinearWrap);
		cmd->set_texture(0, 1, queue.textures[tex]->get_view(), StockSampler::NearestWrap);
		cmd->draw(position.size());
	}
}

void Renderer::upload_texture(Domain domain, const Rect &rect, unsigned off_x, unsigned off_y)
{
	if (domain == Domain::Scaled)
	{
		last_surface = allocator.allocate(domain, {scaling * rect.x, scaling * rect.y, scaling * rect.width,
		                                           scaling * rect.height}, scaling * off_x, scaling * off_y,
		                                  render_state.palette_offset_x, render_state.palette_offset_y);
	}
	else
		last_surface = allocator.allocate(domain, rect, off_x, off_y,
		                                  render_state.palette_offset_x, render_state.palette_offset_y);

	last_surface.texture += queue.textures.size();
	last_uv_scale_x = 1.0f / rect.width;
	last_uv_scale_y = 1.0f / rect.height;
}

void Renderer::blit_vram(const Rect &dst, const Rect &src)
{
	VK_ASSERT(dst.width == src.width);
	VK_ASSERT(dst.height == src.height);
	auto domain = atlas.blit_vram(dst, src);
	ensure_command_buffer();

	struct Push
	{
		uint32_t src_offset[2];
		uint32_t dst_offset[2];
		uint32_t size[2];
		float inv_size[2];
	};

	if (domain == Domain::Scaled)
	{
		cmd->set_program(*pipelines.blit_vram_scaled);
		cmd->set_storage_texture(0, 0, scaled_framebuffer->get_view());
		cmd->set_texture(0, 1, scaled_framebuffer->get_view(), StockSampler::NearestClamp);
		Push push = {
			{scaling * src.x,             scaling * src.y},
			{scaling * dst.x,             scaling * dst.y},
			{scaling * dst.width,         scaling * dst.height},
			{1.0f / (scaling * FB_WIDTH), 1.0f / (scaling * FB_HEIGHT)},
		};
		cmd->push_constants(&push, 0, sizeof(push));
		cmd->dispatch((scaling * dst.width + 7) >> 3, (scaling * dst.height + 7) >> 3, 1);
	} else
	{
		cmd->set_program(*pipelines.blit_vram_unscaled);
		cmd->set_storage_texture(0, 0, framebuffer->get_view());
		cmd->set_texture(0, 1, framebuffer->get_view(), StockSampler::NearestClamp);
		Push push = {
			{src.x,           src.y},
			{dst.x,           dst.y},
			{dst.width,       dst.height},
			{1.0f / FB_WIDTH, 1.0f / FB_HEIGHT},
		};
		cmd->push_constants(&push, 0, sizeof(push));
		cmd->dispatch((dst.width + 7) >> 3, (dst.height + 7) >> 3, 1);
	}
}

void Renderer::copy_cpu_to_vram(const uint16_t *data, const Rect &rect)
{
	atlas.write_compute(Domain::Unscaled, rect);
	VkDeviceSize size = rect.width * rect.height * sizeof(uint16_t);

	// TODO: Chain allocate this.
	auto buffer = device.create_buffer({BufferDomain::Host, size, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT}, data);
	BufferViewCreateInfo view_info = {};
	view_info.buffer = buffer.get();
	view_info.offset = 0;
	view_info.range = size;
	view_info.format = VK_FORMAT_R16_UINT;
	auto view = device.create_buffer_view(view_info);

	ensure_command_buffer();
	cmd->set_program(*pipelines.copy_to_vram);
	cmd->set_storage_texture(0, 0, framebuffer->get_view());
	cmd->set_buffer_view(0, 1, *view);

	struct Push
	{
		Rect rect;
		uint32_t offset;
	};
	Push push = {rect, 0};
	cmd->push_constants(&push, 0, sizeof(push));

	// TODO: Batch up work.
	cmd->dispatch((rect.width + 7) >> 3, (rect.height + 7) >> 3, 1);
}

Renderer::~Renderer()
{
	if (cmd)
		device.submit(cmd);
}

void Renderer::flush_texture_allocator()
{
	allocator.end(cmd.get(), scaled_framebuffer->get_view(), framebuffer->get_view());
	unsigned num_textures = allocator.get_num_textures();
	for (unsigned i = 0; i < num_textures; i++)
		queue.textures.push_back(allocator.get_image(i));
	allocator.begin();
}

void Renderer::reset_queue()
{
	queue.opaque_position.clear();
	queue.opaque_attrib.clear();
	queue.opaque_textured_attrib.clear();
	queue.opaque_textured_position.clear();
	queue.textures.clear();
}

}
