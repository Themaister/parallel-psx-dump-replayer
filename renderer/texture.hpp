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
	Rect rect;
	uint32_t base_x;
	uint32_t base_y;
	uint32_t mask;
	uint32_t layer;
};

static const unsigned NUM_TEXTURES = 8 * 8;

class TextureAllocator
{
public:
	TextureAllocator(Vulkan::Device &device);

	void begin();

	TextureSurface allocate(Domain domain, const Rect &rect, unsigned off_x, unsigned off_y);
	void end(Vulkan::CommandBuffer *cmd, const Vulkan::ImageView &scaled, const Vulkan::ImageView &unscaled);
	inline Vulkan::ImageHandle get_image(unsigned index)
	{
		return images[index];
	}

	inline unsigned get_num_textures() const
	{
		return texture_count;
	}

private:
	Vulkan::Device &device;
	int size_to_texture_map[NUM_TEXTURES];
	unsigned widths[NUM_TEXTURES];
	unsigned heights[NUM_TEXTURES];
	unsigned array_count[NUM_TEXTURES];
	std::vector<BlitInfo> scaled_blits[NUM_TEXTURES];
	std::vector<BlitInfo> unscaled_blits[NUM_TEXTURES];
	unsigned texture_count = 0;

	Vulkan::ImageHandle images[NUM_TEXTURES];
	Vulkan::ProgramHandle scaled_blitter;
	Vulkan::ProgramHandle unscaled_blitter;
};
}