#pragma once

#include "atlas.hpp"
#include "device.hpp"

namespace PSX
{
struct TextureSurface
{
	unsigned texture;
	unsigned layer;
};

struct BlitInfo
{
	uint32_t base;
	uint32_t pal_base;
	uint32_t mask;
	uint32_t layer;
};

static const unsigned NUM_TEXTURES = 8 * 8;

class TextureAllocator
{
public:
	TextureAllocator(Vulkan::Device &device);

	void begin();

	TextureSurface allocate(Domain domain, const Rect &rect, unsigned off_x, unsigned off_y, unsigned pal_off_x, unsigned pal_off_y);
	void end(Vulkan::CommandBuffer *cmd, const Vulkan::ImageView &scaled, const Vulkan::ImageView &unscaled);
	inline Vulkan::ImageHandle get_image(unsigned index)
	{
		return images[index];
	}

	inline unsigned get_num_textures() const
	{
		return texture_count;
	}

	inline void set_texture_mode(TextureMode mode)
	{
		texture_mode = mode;
	}

private:
	Vulkan::Device &device;
	int size_to_texture_map[NUM_TEXTURES];
	unsigned widths[NUM_TEXTURES];
	unsigned heights[NUM_TEXTURES];
	unsigned array_count[NUM_TEXTURES];
	std::vector<BlitInfo> scaled_blits[NUM_TEXTURES];
	std::vector<BlitInfo> unscaled_blits[NUM_TEXTURES];
	std::vector<BlitInfo> pal4_blits[NUM_TEXTURES];
	std::vector<BlitInfo> pal8_blits[NUM_TEXTURES];
	unsigned texture_count = 0;
	TextureMode texture_mode = TextureMode::None;

	Vulkan::ImageHandle images[NUM_TEXTURES];
	Vulkan::ProgramHandle scaled_blitter;
	Vulkan::ProgramHandle unscaled_blitter;
	Vulkan::ProgramHandle pal4_blitter;
	Vulkan::ProgramHandle pal8_blitter;
};
}