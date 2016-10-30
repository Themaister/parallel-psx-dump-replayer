#include <cstring>
#include "texture.hpp"
#include "util.hpp"

using namespace Vulkan;
using namespace std;

namespace PSX
{
TextureAllocator::TextureAllocator(Vulkan::Device &device)
	: device(device)
{
	static const uint32_t upload_scaled_comp[] =
#include "upload.scaled.comp.inc"
	;
	static const uint32_t upload_unscaled_comp[] =
#include "upload.unscaled.comp.inc"
	;
	static const uint32_t upload_pal4_comp[] =
#include "upload.pal4.comp.inc"
	;
	static const uint32_t upload_pal8_comp[] =
#include "upload.pal8.comp.inc"
	;
	scaled_blitter = device.create_program(upload_scaled_comp, sizeof(upload_scaled_comp));
	unscaled_blitter = device.create_program(upload_unscaled_comp, sizeof(upload_unscaled_comp));
	pal4_blitter = device.create_program(upload_pal4_comp, sizeof(upload_pal4_comp));
	pal8_blitter = device.create_program(upload_pal8_comp, sizeof(upload_pal8_comp));

	for (auto &i : size_to_texture_map)
		i = -1;
}

void TextureAllocator::begin()
{
	for (unsigned i = 0; i < texture_count; i++)
	{
		images[i].reset();
		scaled_blits[i].clear();
		unscaled_blits[i].clear();
	}

	memset(widths, 0, texture_count * sizeof(widths[0]));
	memset(heights, 0, texture_count * sizeof(heights[0]));
	memset(array_count, 0, texture_count * sizeof(array_count[0]));
	for (auto &i : size_to_texture_map)
		i = -1;

	texture_count = 0;
}

TextureSurface TextureAllocator::allocate(Domain domain, const Rect &rect, unsigned off_x, unsigned off_y, unsigned pal_off_x, unsigned pal_off_y)
{
	// Sizes are always POT, minimum 8, maximum 256 * max scaling (8).
	unsigned xkey = trailing_zeroes(rect.width) - 3;
	unsigned ykey = trailing_zeroes(rect.height) - 3;
	unsigned key = ykey * 8 + xkey;

	auto &map = size_to_texture_map[key];
	if (map == -1)
	{
		map = texture_count;
		widths[texture_count] = rect.width;
		heights[texture_count] = rect.height;
		array_count[texture_count] = 0;
		texture_count++;
	}
	unsigned layer = array_count[map]++;

	auto pack2x16 = [](uint32_t x, uint32_t y) {
		return x | (y << 16);
	};

	if (domain == Domain::Scaled)
		scaled_blits[map].push_back({ rect, pack2x16(off_x, off_y), 0, 0, layer });
	else if (texture_mode == TextureMode::Palette4bpp)
		pal4_blits[map].push_back({ rect, pack2x16(off_x, off_y), pack2x16(pal_off_x, pal_off_y), 0, layer });
	else if (texture_mode == TextureMode::Palette8bpp)
		pal8_blits[map].push_back({ rect, pack2x16(off_x, off_y), pack2x16(pal_off_x, pal_off_y), 0, layer });
	else
		unscaled_blits[map].push_back({ rect, pack2x16(off_x, off_y), 0, 0, layer });

	return { unsigned(map), layer };
}

void TextureAllocator::end(CommandBuffer *cmd, const ImageView &scaled, const ImageView &unscaled)
{
	if (!texture_count)
		return;

	auto info = ImageCreateInfo::immutable_2d_image(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 1);
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.misc = IMAGE_MISC_FORCE_ARRAY_BIT;
	for (unsigned i = 0; i < texture_count; i++)
	{
		info.width = widths[i];
		info.height = heights[i];
		info.layers = array_count[i];
		images[i] = device.create_image(info);
	}

	const auto issue_blits = [this, cmd](const std::vector<BlitInfo> *infos) {
		for (unsigned i = 0; i < texture_count; i++)
		{
			if (!infos[i].empty())
			{
				cmd->set_storage_texture(1, 0, images[i]->get_view());
				void *ptr = cmd->allocate_constant_data(1, 1, infos[i].size() * sizeof(BlitInfo));
				memcpy(ptr, infos[i].data(), infos[i].size() * sizeof(BlitInfo));
				cmd->dispatch(widths[i] >> 3, heights[i] >> 3, infos[i].size());
			}
		}
	};

	cmd->set_program(*scaled_blitter);
	cmd->set_texture(0, 0, scaled, StockSampler::NearestClamp);
	issue_blits(scaled_blits);
	cmd->set_program(*unscaled_blitter);
	cmd->set_texture(0, 0, unscaled, StockSampler::NearestClamp);
	issue_blits(unscaled_blits);
	cmd->set_program(*pal4_blitter);
	issue_blits(pal4_blits);
	cmd->set_program(*pal8_blitter);
	issue_blits(pal8_blits);

	for (unsigned i = 0; i < texture_count; i++)
	{
		cmd->image_barrier(*images[i], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		images[i]->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
}
}