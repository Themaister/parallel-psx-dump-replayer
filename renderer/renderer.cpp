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
		VK_IMAGE_USAGE_STORAGE_BIT;

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

	static const uint32_t quad_vert[] =
#include "quad.vert.inc"
	;
	static const uint32_t quad_frag[] =
#include "quad.frag.inc"
	;
	quad_blitter = device.create_program(quad_vert, sizeof(quad_vert), quad_frag, sizeof(quad_frag));

	cmd = device.request_command_buffer();
	VkClearValue color = {};
	color.color.float32[1] = 1.0f;
	cmd->clear_image(*scaled_framebuffer, color);
	cmd->image_barrier(*scaled_framebuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
	device.submit(cmd);
	cmd.reset();
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
	cmd->set_texture(0, 0, scaled_framebuffer->get_view(), StockSampler::LinearClamp);
	cmd->bind_program(*quad_blitter);
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

Renderer::~Renderer()
{
}

}