#pragma once

#include <stdint.h>
#include <vector>

namespace PSX
{
static const unsigned FB_WIDTH = 1024;
static const unsigned FB_HEIGHT = 512;
static const unsigned BLOCK_WIDTH = 8;
static const unsigned BLOCK_HEIGHT = 8;
static const unsigned NUM_BLOCKS_X = FB_WIDTH / BLOCK_WIDTH;
static const unsigned NUM_BLOCKS_Y = FB_HEIGHT / BLOCK_HEIGHT;

enum class Domain : unsigned
{
	Unscaled,
	Scaled
};

enum class Stage : unsigned
{
	Compute,
	Transfer,
	Fragment
};

enum class TextureMode
{
	None,
	Palette4bpp,
	Palette8bpp,
	ABGR1555
};

struct Rect
{
	unsigned x = 0;
	unsigned y = 0;
	unsigned width = 0;
	unsigned height = 0;

	inline bool operator==(const Rect &rect) const
	{
		return x == rect.x && y == rect.y && width == rect.width && height == rect.height;
	}

	inline bool operator!=(const Rect &rect) const
	{
		return x != rect.x || y != rect.y || width != rect.width || height != rect.height;
	}

	inline bool contains(const Rect &rect) const
	{
		return x <= rect.x && y <= rect.y && (x + width) >= (rect.x + rect.width) &&
		       (y + height) >= (rect.y + rect.height);
	}

	inline bool intersects(const Rect &rect) const
	{
		bool horiz = ((x + width) > rect.x) || ((rect.x + rect.width) > x);
		bool vert = ((y + height) > rect.y) || ((rect.y + rect.height) > y);
		return horiz && vert;
	}
};

using FBColor = uint32_t;

static inline uint32_t fbcolor_to_rgba8(FBColor color)
{
	return color;
}

static inline void fbcolor_to_rgba32f(float *v, FBColor color)
{
	unsigned r = (color >> 0) & 0xff;
	unsigned g = (color >> 8) & 0xff;
	unsigned b = (color >> 16) & 0xff;
	v[0] = r * (1.0f / 255.0f);
	v[1] = g * (1.0f / 255.0f);
	v[2] = b * (1.0f / 255.0f);
	// Not sure what happens to mask bit.
	v[3] = 0.0f;
}

enum StatusFlag
{
	STATUS_FB_ONLY = 0,
	STATUS_FB_PREFER = 1,
	STATUS_SFB_ONLY = 2,
	STATUS_SFB_PREFER = 3,
	STATUS_OWNERSHIP_MASK = 3,

	STATUS_COMPUTE_FB_READ = 1 << 2,
	STATUS_COMPUTE_FB_WRITE = 1 << 3,
	STATUS_COMPUTE_SFB_READ = 1 << 4,
	STATUS_COMPUTE_SFB_WRITE = 1 << 5,

	STATUS_TRANSFER_FB_READ = 1 << 6,
	STATUS_TRANSFER_SFB_READ = 1 << 7,
	STATUS_TRANSFER_FB_WRITE = 1 << 8,
	STATUS_TRANSFER_SFB_WRITE = 1 << 9,

	STATUS_FRAGMENT_SFB_READ = 1 << 10,
	STATUS_FRAGMENT_SFB_WRITE = 1 << 11,
	STATUS_FRAGMENT_FB_READ = 1 << 12,
	STATUS_FRAGMENT_FB_WRITE = 1 << 13,

	STATUS_FB_READ = STATUS_COMPUTE_FB_READ | STATUS_TRANSFER_FB_READ | STATUS_FRAGMENT_FB_READ,
	STATUS_FB_WRITE = STATUS_COMPUTE_FB_WRITE | STATUS_TRANSFER_FB_WRITE | STATUS_FRAGMENT_FB_WRITE,
	STATUS_SFB_READ = STATUS_COMPUTE_SFB_READ | STATUS_TRANSFER_SFB_READ | STATUS_FRAGMENT_SFB_READ,
	STATUS_SFB_WRITE = STATUS_COMPUTE_SFB_WRITE | STATUS_TRANSFER_SFB_WRITE | STATUS_FRAGMENT_SFB_WRITE,
	STATUS_FRAGMENT =
	    STATUS_FRAGMENT_FB_READ | STATUS_FRAGMENT_FB_WRITE | STATUS_FRAGMENT_SFB_READ | STATUS_FRAGMENT_SFB_WRITE,
	STATUS_ALL = STATUS_FB_READ | STATUS_FB_WRITE | STATUS_SFB_READ | STATUS_SFB_WRITE
};
using StatusFlags = uint16_t;

class HazardListener
{
public:
	virtual ~HazardListener() = default;
	virtual void hazard(StatusFlags flags) = 0;
	virtual void resolve(Domain target_domain, unsigned x, unsigned y) = 0;
	virtual void flush_render_pass(const Rect &rect) = 0;
	virtual void discard_render_pass() = 0;
	virtual void upload_texture(Domain target_domain, const Rect &rect, unsigned off_x, unsigned off_y) = 0;
	virtual void clear_quad(const Rect &rect, FBColor color) = 0;
	virtual void clear_quad_separate(const Rect &rect, FBColor color) = 0;
};

class FBAtlas
{
public:
	FBAtlas();

	void set_hazard_listener(HazardListener *hazard)
	{
		listener = hazard;
	}

	void read_compute(Domain domain, const Rect &rect);
	void write_compute(Domain domain, const Rect &rect);
	void read_transfer(Domain domain, const Rect &rect);
	void write_transfer(Domain domain, const Rect &rect);
	void read_fragment(Domain domain, const Rect &rect);
	Domain blit_vram(const Rect &dst, const Rect &src);

	void write_fragment();
	void clear_rect(const Rect &rect, FBColor color);
	void set_draw_rect(const Rect &rect);
	void set_texture_window(const Rect &rect);

	TextureMode set_texture_mode(TextureMode mode)
	{
		std::swap(renderpass.texture_mode, mode);
		return mode;
	}

	void set_texture_offset(unsigned x, unsigned y)
	{
		renderpass.texture_offset_x = x;
		renderpass.texture_offset_y = y;
	}

	void set_palette_offset(unsigned x, unsigned y)
	{
		renderpass.palette_offset_x = x;
		renderpass.palette_offset_y = y;
	}

	bool render_pass_is_clear() const
	{
		return renderpass.clean_clear;
	}

	FBColor render_pass_clear_color() const
	{
		return renderpass.color;
	}

	void pipeline_barrier(StatusFlags domains);
	void notify_external_barrier(StatusFlags domains);

private:
	StatusFlags fb_info[NUM_BLOCKS_X * NUM_BLOCKS_Y];
	HazardListener *listener = nullptr;

	void read_domain(Domain domain, Stage stage, const Rect &rect);
	void write_domain(Domain domain, Stage stage, const Rect &rect);
	void sync_domain(Domain domain, const Rect &rect);
	void read_texture();
	Domain find_suitable_domain(const Rect &rect);

	struct
	{
		Rect rect;
		Rect texture_window;
		unsigned texture_offset_x = 0, texture_offset_y = 0;
		unsigned palette_offset_x = 0, palette_offset_y = 0;
		TextureMode texture_mode = TextureMode::None;
		FBColor color = 0;
		bool inside = false;
		bool clean_clear = false;
	} renderpass;

	StatusFlags &info(unsigned block_x, unsigned block_y)
	{
		return fb_info[NUM_BLOCKS_X * block_y + block_x];
	}

	const StatusFlags &info(unsigned block_x, unsigned block_y) const
	{
		return fb_info[NUM_BLOCKS_X * block_y + block_x];
	}

	void flush_render_pass();
	void discard_render_pass();
	bool inside_render_pass(const Rect &rect);
};
}
