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
	info.format = VK_FORMAT_A1R5G5B5_UNORM_PACK16;
	info.usage =
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	scaled_framebuffer = device.create_image(info);
	info.format = VK_FORMAT_D16_UNORM;
	info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	info.domain = ImageDomain::Transient;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth = device.create_image(info);

	atlas.set_hazard_listener(this);

	init_pipelines();

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
	copy_cpu_to_vram(data, { 16, 16, 8, 8 });
	cmd->image_barrier(*framebuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
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

	pipelines.scaled_quad_blitter = device.create_program(quad_vert, sizeof(quad_vert), scaled_quad_frag, sizeof(scaled_quad_frag));
	pipelines.unscaled_quad_blitter = device.create_program(quad_vert, sizeof(quad_vert), unscaled_quad_frag, sizeof(unscaled_quad_frag));
	pipelines.copy_to_vram = device.create_program(copy_vram_comp, sizeof(copy_vram_comp));
}

void Renderer::set_draw_rect(const Rect &rect)
{
	atlas.set_draw_rect(rect);
}

void Renderer::clear_rect(const Rect &rect)
{
	atlas.clear_rect(rect);
}

void Renderer::set_texture_window(const Rect &rect)
{
	atlas.set_texture_window(rect);
}

void Renderer::scanout(const Rect &rect)
{
	if (!cmd)
		cmd = device.request_command_buffer();

	cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
	cmd->set_quad_state();
	cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestClamp);
	cmd->set_program(*pipelines.unscaled_quad_blitter);
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

}

void Renderer::resolve(Domain target_domain, const Rect &rect)
{

}

void Renderer::discard_render_pass()
{

}

void Renderer::flush_render_pass()
{

}

void Renderer::upload_texture(Domain target_domain, const Rect &rect)
{

}

void Renderer::copy_cpu_to_vram(const uint16_t *data, const Rect &rect)
{
	VK_ASSERT((rect.width & 7) == 0);
	VK_ASSERT((rect.height & 7) == 0);
	atlas.write_compute(Domain::Unscaled, rect);
	if (!cmd)
		cmd = device.request_command_buffer();

	VkDeviceSize size = rect.width * rect.height * sizeof(uint16_t);

	// TODO: Chain allocate this.
	auto buffer = device.create_buffer({ BufferDomain::Host, size, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT }, data);
	BufferViewCreateInfo view_info = {};
	view_info.buffer = buffer.get();
	view_info.range = 0;
	view_info.range = size;
	view_info.format = VK_FORMAT_R16_UINT;
	auto view = device.create_buffer_view(view_info);
	cmd->set_program(*pipelines.copy_to_vram);
	cmd->set_storage_texture(0, 0, framebuffer->get_view());
	cmd->set_buffer_view(0, 1, *view);

	struct Push
	{
		Rect rect;
		uint32_t offset;
	};
	Push push = { rect, 0 };
	cmd->push_constants(&push, 0, sizeof(push));
	cmd->dispatch(rect.width >> 3, rect.height >> 3, 1);
}

Renderer::~Renderer()
{
}

}