#pragma once

#include "vulkan.hpp"
#include "wsi.hpp"
#include "device.hpp"
#include "atlas.hpp"

namespace PSX
{
enum class TextureMode
{
	None,
	Palette4bpp,
	Palette8bpp,
	ABGR1555
};
class Renderer : private HazardListener
{
public:
	Renderer(Vulkan::Device &device, unsigned scaling);
	~Renderer();

	void set_draw_rect(const Rect &rect);
	void clear_rect(const Rect &rect);
	void set_texture_window(const Rect &rect);
	void copy_cpu_to_vram(const uint16_t *data, const Rect &rect);

	void scanout(const Rect &rect);

	inline void set_texture_format(TextureMode mode) { texture_mode = mode; }
	inline void enable_semi_transparent(bool enable) { semi_transparent = enable; }

private:
	Vulkan::Device &device;
	unsigned scaling;
	Vulkan::ImageHandle scaled_framebuffer;
	Vulkan::ImageHandle framebuffer;
	Vulkan::ImageHandle depth;
	FBAtlas atlas;

	Vulkan::CommandBufferHandle cmd;

	void hazard(StatusFlags flags) override;
	void resolve(Domain target_domain, const Rect &rect) override;
	void flush_render_pass() override;
	void discard_render_pass() override;
	void upload_texture(Domain target_domain, const Rect &rect) override;

	TextureMode texture_mode = TextureMode::None;
	bool semi_transparent = false;

	struct
	{
		Vulkan::ProgramHandle copy_to_vram;
		Vulkan::ProgramHandle unscaled_quad_blitter;
		Vulkan::ProgramHandle scaled_quad_blitter;
		Vulkan::ProgramHandle resolve_to_scaled;
		Vulkan::ProgramHandle resolve_to_unscaled;
	} pipelines;

	void init_pipelines();
	void ensure_command_buffer();
};

}