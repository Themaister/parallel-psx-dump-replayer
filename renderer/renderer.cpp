#include <cstring>
#include "renderer.hpp"

using namespace Vulkan;
using namespace std;

namespace PSX
{
Renderer::Renderer(Device &device, unsigned scaling)
	: device(device),
	  scaling(scaling)
{
	auto info = ImageCreateInfo::render_target(FB_WIDTH, FB_HEIGHT, VK_FORMAT_R32_UINT);
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	info.usage =
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	framebuffer = device.create_image(info);
	info.width *= scaling;
	info.height *= scaling;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.usage =
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
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

#define COLOR (31 << 0)
	static const uint16_t data[8 * 8] = {
		COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR,
		COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR,
		COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR,
		COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR,
		COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR,
		COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR,
		COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR,
		COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR, COLOR,
	};
#define COLOR2 (31 << 5)
	static const uint16_t data2[8 * 8] = {
		COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2,
		COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2,
		COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2,
		COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2,
		COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2,
		COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2,
		COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2,
		COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2, COLOR2,
	};

	uint16_t tmp[24 * 8];
	for (auto &t : tmp)
		t = COLOR2;
	tmp[1] = COLOR;

	copy_cpu_to_vram(data, {7, 7, 8, 8});
	copy_cpu_to_vram(data2, {24, 16, 8, 8});
	//copy_cpu_to_vram(data, {16, 40, 8, 8});
	copy_cpu_to_vram(tmp, {29, 40, 24, 8});
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

	pipelines.scaled_quad_blitter = device.create_program(quad_vert, sizeof(quad_vert), scaled_quad_frag,
	                                                      sizeof(scaled_quad_frag));
	pipelines.unscaled_quad_blitter = device.create_program(quad_vert, sizeof(quad_vert), unscaled_quad_frag,
	                                                        sizeof(unscaled_quad_frag));
	pipelines.copy_to_vram = device.create_program(copy_vram_comp, sizeof(copy_vram_comp));
	pipelines.resolve_to_scaled = device.create_program(resolve_to_scaled, sizeof(resolve_to_scaled));
	pipelines.resolve_to_unscaled = device.create_program(resolve_to_unscaled, sizeof(resolve_to_unscaled));
	pipelines.opaque_flat = device.create_program(opaque_flat_vert, sizeof(opaque_flat_vert), opaque_flat_frag, sizeof(opaque_flat_frag));
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
		dst_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
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
		LOG("Resolving scaled (%u, %u, %u, %u)\n", rect.x, rect.y, rect.width, rect.height);
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
	queue.opaque_triangles.clear();
}

void Renderer::draw_triangle(const Vertex *vertices)
{
	atlas.write_fragment(false);
	primitive_index++;
	float z = 1.0f - primitive_index * (1.0f / 0xffff);
	Triangle triangle;
	for (unsigned i = 0; i < 3; i++)
	{
		triangle.vertices[i] = {vertices[i].x + render_state.draw_offset_x, vertices[i].y + render_state.draw_offset_y, z, vertices[i].w,
			 vertices[i].color};
	}
	queue.opaque_triangles.push_back(triangle);
}

void Renderer::clear_quad(const Rect &rect, FBColor color)
{
	primitive_index++;
	float z = 1.0f - primitive_index * (1.0f / 0xffff);
	Triangle triangles[2];
	BufferVertex v0 = { float(rect.x), float(rect.y), z, 1.0f, color };
	BufferVertex v1 = { float(rect.x) + float(rect.width), float(rect.y), z, 1.0f, color };
	BufferVertex v2 = { float(rect.x), float(rect.y) + float(rect.height), z, 1.0f, color };
	BufferVertex v3 = { float(rect.x) + float(rect.width), float(rect.y) + float(rect.height), z, 1.0f, color };
	triangles[0].vertices[0] = v0;
	triangles[0].vertices[1] = v1;
	triangles[0].vertices[2] = v2;
	triangles[1].vertices[0] = v3;
	triangles[1].vertices[1] = v2;
	triangles[1].vertices[2] = v1;
	queue.opaque_triangles.push_back(triangles[0]);
	queue.opaque_triangles.push_back(triangles[1]);
}

void Renderer::flush_render_pass(const Rect &rect)
{
	primitive_index = 0;
	ensure_command_buffer();
	bool is_clear = atlas.render_pass_is_clear();
	FBColor color = atlas.render_pass_clear_color();

	RenderPassInfo info = {};
	info.clear_color[0].float32[0] = ((color >> 0) & 0x1f) * (1.0f / 31.0f);
	info.clear_color[0].float32[1] = ((color >> 5) & 0x1f) * (1.0f / 31.0f);
	info.clear_color[0].float32[2] = ((color >> 10) & 0x1f) * (1.0f / 31.0f);
	info.clear_color[0].float32[3] = ((color >> 15) & 0x1) * (1.0f / 1.0f);
	info.clear_depth_stencil = {1.0f, 0};
	info.color_attachments[0] = &scaled_framebuffer->get_view();
	info.depth_stencil = &depth->get_view();
	info.num_color_attachments = 1;

	info.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT | RENDER_PASS_OP_STORE_COLOR_BIT |
	                RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT;
	if (is_clear)
		info.op_flags |= RENDER_PASS_OP_CLEAR_COLOR_BIT;
	else
		info.op_flags |= RENDER_PASS_OP_LOAD_COLOR_BIT;

	info.render_area.offset = {rect.x * scaling, rect.y * scaling};
	info.render_area.extent = {rect.width * scaling, rect.height * scaling};
	cmd->begin_render_pass(info);
	cmd->set_scissor(info.render_area);

	render_opaque_primitives();

	cmd->end_render_pass();

	// Render passes are implicitly synchronized.
	cmd->image_barrier(*scaled_framebuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
}

void Renderer::render_opaque_primitives()
{
	if (queue.opaque_triangles.empty())
		return;

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);

	// Render flat-shaded primitives.
	auto *buffer = static_cast<BufferVertex *>(cmd->allocate_vertex_data(0, queue.opaque_triangles.size() *
	                                                                        sizeof(Triangle), sizeof(BufferVertex)));
	for (auto i = queue.opaque_triangles.size(); i; i--)
	{
		*buffer++ = queue.opaque_triangles[i - 1].vertices[0];
		*buffer++ = queue.opaque_triangles[i - 1].vertices[1];
		*buffer++ = queue.opaque_triangles[i - 1].vertices[2];
	}
	// Position
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	// Color
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));

	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_program(*pipelines.opaque_flat);
	cmd->draw(queue.opaque_triangles.size() * 3);
	queue.opaque_triangles.clear();
}

void Renderer::upload_texture(Domain target_domain, const Rect &rect)
{

}

void Renderer::copy_cpu_to_vram(const uint16_t *data, const Rect &rect)
{
	VK_ASSERT((rect.width & 7) == 0);
	VK_ASSERT((rect.height & 7) == 0);
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
	cmd->dispatch(rect.width >> 3, rect.height >> 3, 1);
}

Renderer::~Renderer()
{
	if (cmd)
		device.submit(cmd);
}

}