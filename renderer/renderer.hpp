#pragma once

#include "atlas.hpp"
#include "device.hpp"
#include "texture.hpp"
#include "vulkan.hpp"

#ifdef VULKAN_WSI
#include "wsi.hpp"
#endif

#include <string.h>

namespace PSX
{

struct Vertex
{
	float x, y, w;
	uint32_t color;
	uint16_t u, v;
};

struct TextureWindow
{
	uint8_t mask_x, mask_y, or_x, or_y;
};

enum class SemiTransparentMode
{
	None,
	Average,
	Add,
	Sub,
	AddQuarter
};

class Renderer : private HazardListener
{
public:
	struct RenderState
	{
		Rect display_mode;
		TextureWindow texture_window;
		Rect cached_window_rect;
		Rect draw_rect;
		int draw_offset_x = 0;
		int draw_offset_y = 0;
		unsigned palette_offset_x = 0;
		unsigned palette_offset_y = 0;
		unsigned texture_offset_x = 0;
		unsigned texture_offset_y = 0;

		TextureMode texture_mode = TextureMode::None;
		SemiTransparentMode semi_transparent = SemiTransparentMode::None;
		bool force_mask_bit = false;
		bool texture_color_modulate = false;
		bool mask_test = false;
		bool display_on = false;
		bool bpp24 = false;
		bool dither = false;
	};

	struct SaveState
	{
		std::vector<uint32_t> vram;
		RenderState state;
	};

	Renderer(Vulkan::Device &device, unsigned scaling, const SaveState *save_state);
	~Renderer();

	void set_draw_rect(const Rect &rect);
	inline void set_draw_offset(int x, int y)
	{
		render_state.draw_offset_x = x;
		render_state.draw_offset_y = y;
	}

	void set_texture_window(const TextureWindow &rect);
	inline void set_texture_offset(unsigned x, unsigned y)
	{
		atlas.set_texture_offset(x, y);
		render_state.texture_offset_x = x;
		render_state.texture_offset_y = y;
	}

	inline void set_palette_offset(unsigned x, unsigned y)
	{
		atlas.set_palette_offset(x, y);
		render_state.palette_offset_x = x;
		render_state.palette_offset_y = y;
	}

	Vulkan::BufferHandle copy_cpu_to_vram(const Rect &rect);
	uint16_t *begin_copy(Vulkan::BufferHandle handle);
	void end_copy(Vulkan::BufferHandle handle);

	void blit_vram(const Rect &dst, const Rect &src);

	void set_display_mode(const Rect &rect, bool bpp24)
	{
		if (rect != render_state.display_mode || bpp24 != render_state.bpp24)
			last_scanout.reset();

		render_state.display_mode = rect;
		render_state.bpp24 = bpp24;
	}

	void toggle_display(bool enable)
	{
		if (enable != render_state.display_on)
			last_scanout.reset();

		render_state.display_on = enable;
	}

	void set_dither(bool dither)
	{
		render_state.dither = dither;
	}

	void set_scanout_semaphore(Vulkan::Semaphore semaphore);
	void scanout();
	Vulkan::BufferHandle scanout_to_buffer(bool draw_area, unsigned &width, unsigned &height);
	Vulkan::BufferHandle scanout_vram_to_buffer(unsigned &width, unsigned &height);
	Vulkan::ImageHandle scanout_to_texture();

	inline void set_texture_mode(TextureMode mode)
	{
		render_state.texture_mode = mode;
		atlas.set_texture_mode(mode);
		allocator.set_texture_mode(mode);
	}

	inline void set_semi_transparent(SemiTransparentMode state)
	{
		render_state.semi_transparent = state;
	}

	inline void set_force_mask_bit(bool enable)
	{
		render_state.force_mask_bit = enable;
	}

	inline void set_mask_test(bool enable)
	{
		render_state.mask_test = enable;
	}

	inline void set_texture_color_modulate(bool enable)
	{
		render_state.texture_color_modulate = enable;
	}

	// Draw commands
	void clear_rect(const Rect &rect, FBColor color);
	void draw_line(const Vertex *vertices);
	void draw_triangle(const Vertex *vertices);
	void draw_quad(const Vertex *vertices);

	SaveState save_vram_state();

	void reset_counters()
	{
		memset(&counters, 0, sizeof(counters));
	}

	void flush()
	{
		if (cmd)
			device.submit(cmd);
		cmd.reset();
		device.flush_frame();
	}

	struct
	{
		unsigned render_passes = 0;
		unsigned draw_calls = 0;
		unsigned texture_flushes = 0;
		unsigned vertices = 0;
		unsigned native_draw_calls = 0;
	} counters;

private:
	Vulkan::Device &device;
	unsigned scaling;
	Vulkan::ImageHandle scaled_framebuffer;
	Vulkan::ImageHandle framebuffer;
	Vulkan::ImageHandle depth;
	Vulkan::Semaphore scanout_semaphore;
	std::vector<Vulkan::ImageViewHandle> scaled_views;
	FBAtlas atlas;

	Vulkan::CommandBufferHandle cmd;

	void hazard(StatusFlags flags) override;
	void resolve(Domain target_domain, unsigned x, unsigned y) override;
	void flush_render_pass(const Rect &rect) override;
	void discard_render_pass() override;
	void upload_texture(Domain target_domain, const Rect &rect, unsigned off_x, unsigned off_y) override;
	void clear_quad(const Rect &rect, FBColor color) override;
	void clear_quad_separate(const Rect &rect, FBColor color) override;

	struct
	{
		Vulkan::ProgramHandle copy_to_vram;
		Vulkan::ProgramHandle copy_to_vram_masked;
		Vulkan::ProgramHandle unscaled_quad_blitter;
		Vulkan::ProgramHandle scaled_quad_blitter;
		Vulkan::ProgramHandle bpp24_quad_blitter;
		Vulkan::ProgramHandle resolve_to_scaled;
		Vulkan::ProgramHandle resolve_to_unscaled;
		Vulkan::ProgramHandle blit_vram_unscaled;
		Vulkan::ProgramHandle blit_vram_scaled;
		Vulkan::ProgramHandle blit_vram_unscaled_masked;
		Vulkan::ProgramHandle blit_vram_scaled_masked;
		Vulkan::ProgramHandle opaque_flat;
		Vulkan::ProgramHandle opaque_textured;
		Vulkan::ProgramHandle opaque_semi_transparent;
		Vulkan::ProgramHandle semi_transparent;
		Vulkan::ProgramHandle semi_transparent_masked_add;
		Vulkan::ProgramHandle semi_transparent_masked_average;
		Vulkan::ProgramHandle semi_transparent_masked_sub;
		Vulkan::ProgramHandle semi_transparent_masked_add_quarter;
		Vulkan::ProgramHandle flat_masked_add;
		Vulkan::ProgramHandle flat_masked_average;
		Vulkan::ProgramHandle flat_masked_sub;
		Vulkan::ProgramHandle flat_masked_add_quarter;

		Vulkan::ProgramHandle mipmap_resolve;
		Vulkan::ProgramHandle mipmap_energy_first;
		Vulkan::ProgramHandle mipmap_energy;
	} pipelines;

	Vulkan::ImageHandle dither_lut;

	void init_pipelines();
	void ensure_command_buffer();

	RenderState render_state;

	struct BufferVertex
	{
		float x, y, z, w;
#ifndef VRAM_ATLAS
		float u, v;
		float layer;
#endif
		uint32_t color;

#ifdef VRAM_ATLAS
		TextureWindow window;
		int16_t pal_x, pal_y, params;
		int16_t u, v, base_uv_x, base_uv_y;
#endif
	};

	struct BlitInfo
	{
		uint32_t src_offset[2];
		uint32_t dst_offset[2];
		uint32_t extent[2];
		uint32_t padding[2];
	};

	struct SemiTransparentState
	{
		unsigned image_index;
		SemiTransparentMode semi_transparent;
		bool textured;
		bool masked;

		bool operator==(const SemiTransparentState &other) const
		{
			return image_index == other.image_index && semi_transparent == other.semi_transparent &&
			       textured == other.textured && masked == other.masked;
		}

		bool operator!=(const SemiTransparentState &other) const
		{
			return !(*this == other);
		}
	};

	struct OpaqueQueue
	{
		// Non-textured primitives.
		std::vector<BufferVertex> opaque;

// Textured primitives, no semi-transparency.
#ifdef VRAM_ATLAS
		std::vector<BufferVertex> opaque_textured;
#else
		std::vector<std::vector<BufferVertex>> opaque_textured;
#endif

// Textured primitives, semi-transparency enabled.
#ifdef VRAM_ATLAS
		std::vector<BufferVertex> semi_transparent_opaque;
#else
		std::vector<std::vector<BufferVertex>> semi_transparent_opaque;
#endif

		std::vector<BufferVertex> semi_transparent;
		std::vector<SemiTransparentState> semi_transparent_state;

		std::vector<Vulkan::ImageHandle> textures;

		std::vector<VkRect2D> scaled_resolves;
		std::vector<VkRect2D> unscaled_resolves;
		std::vector<BlitInfo> scaled_blits;
		std::vector<BlitInfo> scaled_masked_blits;
		std::vector<BlitInfo> unscaled_blits;
		std::vector<BlitInfo> unscaled_masked_blits;
	} queue;
	unsigned primitive_index = 0;
	bool render_pass_is_feedback = false;
	TextureSurface last_surface;
	float last_uv_scale_x, last_uv_scale_y;

	void render_opaque_primitives();
	void render_opaque_texture_primitives();
	void render_semi_transparent_opaque_texture_primitives();
	void render_semi_transparent_primitives();
	void reset_queue();

	float allocate_depth();
	void flush_texture_allocator();
	TextureAllocator allocator;

	void build_attribs(BufferVertex *verts, const Vertex *vertices, unsigned count);
	bool build_line_quad(Vertex *quad, const Vertex *line);
	std::vector<BufferVertex> *select_pipeline();

	void flush_resolves();
	void flush_blits();

	Rect compute_window_rect(const TextureWindow &window);

	Vulkan::ImageHandle last_scanout;
	Vulkan::ImageHandle reuseable_scanout;

	void mipmap_framebuffer();
	Vulkan::BufferHandle quad;
};
}
