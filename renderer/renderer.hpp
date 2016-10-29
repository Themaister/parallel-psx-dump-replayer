#pragma once

#include "atlas.hpp"
#include "device.hpp"
#include "vulkan.hpp"
#include "wsi.hpp"
#include "texture.hpp"

namespace PSX
{
enum class TextureMode
{
	None,
	Palette4bpp,
	Palette8bpp,
	ABGR1555
};

struct Vertex
{
	float x, y, w;
	uint32_t color;
	uint8_t u, v;
};

class Renderer : private HazardListener
{
public:
	Renderer(Vulkan::Device &device, unsigned scaling);
	~Renderer();

	void set_draw_rect(const Rect &rect);
	inline void set_draw_offset(int x, int y)
	{
		render_state.draw_offset_x = x;
		render_state.draw_offset_y = y;
	}

	void set_texture_window(const Rect &rect);
	inline void set_texture_offset(unsigned x, unsigned y)
	{
		atlas.set_texture_offset(x, y);
	}

	void copy_cpu_to_vram(const uint16_t *data, const Rect &rect);
	void blit_vram(const Rect &dst, const Rect &src);

	void scanout(const Rect &rect);

	inline void set_texture_format(TextureMode mode)
	{
		texture_mode = mode;
	}

	inline void enable_semi_transparent(bool enable)
	{
		semi_transparent = enable;
	}

	// Draw commands
	void clear_rect(const Rect &rect, FBColor color);
	void draw_triangle(const Vertex *vertices);
	void draw_quad(const Vertex *vertices);

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
	void flush_render_pass(const Rect &rect) override;
	void discard_render_pass() override;
	void upload_texture(Domain target_domain, const Rect &rect, unsigned off_x, unsigned off_y) override;
	void clear_quad(const Rect &rect, FBColor color) override;

	TextureMode texture_mode = TextureMode::None;
	bool semi_transparent = false;

	struct
	{
		Vulkan::ProgramHandle copy_to_vram;
		Vulkan::ProgramHandle unscaled_quad_blitter;
		Vulkan::ProgramHandle scaled_quad_blitter;
		Vulkan::ProgramHandle resolve_to_scaled;
		Vulkan::ProgramHandle resolve_to_unscaled;
		Vulkan::ProgramHandle blit_vram_unscaled;
		Vulkan::ProgramHandle blit_vram_scaled;
		Vulkan::ProgramHandle opaque_flat;
		Vulkan::ProgramHandle opaque_textured;
	} pipelines;

	void init_pipelines();
	void ensure_command_buffer();

	struct
	{
		int draw_offset_x = 0;
		int draw_offset_y = 0;
	} render_state;

	struct BufferPosition
	{
		float x, y, z, w;
	};

	struct BufferAttrib
	{
		float u, v, layer;
		uint32_t color;
	};

	struct OpaqueQueue
	{
		// Non-textured primitives.
		std::vector<BufferPosition> opaque_position;
		std::vector<BufferAttrib> opaque_attrib;

		// Textured primitives, no semi-transparency.
		std::vector<std::vector<BufferPosition>> opaque_textured_position;
		std::vector<std::vector<BufferAttrib>> opaque_textured_attrib;

		std::vector<Vulkan::ImageHandle> textures;
	} queue;
	unsigned primitive_index = 0;
	TextureSurface last_surface;
	float last_uv_scale_x, last_uv_scale_y;

	void render_opaque_primitives();
	void render_opaque_texture_primitives();
	void reset_queue();

	float allocate_depth(bool reads_window);
	void flush_texture_allocator();
	TextureAllocator allocator;
};
}