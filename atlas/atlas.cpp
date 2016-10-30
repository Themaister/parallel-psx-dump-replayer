#include "atlas.hpp"
#include <algorithm>

using namespace std;

namespace PSX
{

FBAtlas::FBAtlas()
{
	for (auto &f : fb_info)
		f = STATUS_FB_PREFER;
}

Domain FBAtlas::blit_vram(const Rect &dst, const Rect &src)
{
	auto src_domain = find_suitable_domain(src);
	auto dst_domain = find_suitable_domain(dst);
	Domain domain;
	if (src_domain != dst_domain)
		domain = Domain::Unscaled;
	else
		domain = src_domain;

	sync_domain(domain, src);
	sync_domain(domain, dst);
	read_domain(domain, Stage::Compute, src);
	write_domain(domain, Stage::Compute, dst);
	return domain;
}

void FBAtlas::read_fragment(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	read_domain(domain, Stage::Fragment, rect);
}

void FBAtlas::read_compute(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	read_domain(domain, Stage::Compute, rect);
}

void FBAtlas::write_compute(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	write_domain(domain, Stage::Compute, rect);
}

void FBAtlas::read_transfer(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	read_domain(domain, Stage::Transfer, rect);
}

void FBAtlas::write_transfer(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	write_domain(domain, Stage::Transfer, rect);
}

void FBAtlas::read_texture()
{
	auto shifted = renderpass.texture_window;
	bool palette;
	switch (renderpass.texture_mode)
	{
	case TextureMode::Palette4bpp:
		shifted.x >>= 2;
		shifted.width >>= 2;
		palette = true;
		break;

	case TextureMode::Palette8bpp:
		shifted.x >>= 1;
		shifted.width >>= 1;
		palette = true;
		break;

	default:
		palette = false;
		break;
	}
	shifted.x += renderpass.texture_offset_x;
	shifted.y += renderpass.texture_offset_y;

	auto domain = palette ? Domain::Unscaled : find_suitable_domain(shifted);
	sync_domain(domain, shifted);

	const Rect palette_rect = { renderpass.palette_offset_x, renderpass.palette_offset_y,
		                        renderpass.texture_mode == TextureMode::Palette8bpp ? 256u : 16u, 1 };

	if (palette)
		sync_domain(domain, palette_rect);

	read_domain(domain, Stage::Compute, shifted);
	if (palette)
		read_domain(domain, Stage::Compute, palette_rect);
	listener->upload_texture(domain, renderpass.texture_window, renderpass.texture_offset_x,
	                         renderpass.texture_offset_y);
}

void FBAtlas::write_domain(Domain domain, Stage stage, const Rect &rect)
{
	if (inside_render_pass(rect))
		flush_render_pass();

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	unsigned write_domains = 0;
	unsigned hazard_domains;
	unsigned resolve_domains;
	if (domain == Domain::Unscaled)
	{
		hazard_domains = STATUS_FB_WRITE | STATUS_FB_READ;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_FB_WRITE | STATUS_FB_ONLY;
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_FB_WRITE | STATUS_FB_ONLY;
		else if (stage == Stage::Fragment)
		{
			resolve_domains = STATUS_FRAGMENT_FB_WRITE | STATUS_FB_ONLY;
			hazard_domains &= ~STATUS_FRAGMENT;
		}
	}
	else
	{
		hazard_domains = STATUS_SFB_WRITE | STATUS_SFB_READ;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_SFB_WRITE;
		else if (stage == Stage::Fragment)
		{
			hazard_domains &= ~STATUS_FRAGMENT;
			resolve_domains = STATUS_FRAGMENT_SFB_WRITE;
		}
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_SFB_WRITE;
		resolve_domains |= STATUS_SFB_ONLY;
	}

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			write_domains |= info(x, y) & hazard_domains;

	if (write_domains)
		pipeline_barrier(write_domains);

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) = (info(x, y) & ~STATUS_OWNERSHIP_MASK) | resolve_domains;
}

void FBAtlas::read_domain(Domain domain, Stage stage, const Rect &rect)
{
	if (inside_render_pass(rect))
		flush_render_pass();

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	unsigned write_domains = 0;
	unsigned hazard_domains;
	unsigned resolve_domains;
	if (domain == Domain::Unscaled)
	{
		hazard_domains = STATUS_FB_WRITE;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_FB_READ;
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_FB_READ;
		else if (stage == Stage::Fragment)
		{
			resolve_domains = STATUS_FRAGMENT_FB_READ;
			hazard_domains &= ~STATUS_FRAGMENT;
		}
	}
	else
	{
		hazard_domains = STATUS_SFB_WRITE;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_SFB_READ;
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_SFB_READ;
		else if (stage == Stage::Fragment)
		{
			hazard_domains &= ~STATUS_FRAGMENT;
			resolve_domains = STATUS_FRAGMENT_SFB_READ;
		}
	}

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			write_domains |= info(x, y) & hazard_domains;

	if (write_domains)
		pipeline_barrier(write_domains);

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) |= resolve_domains;
}

void FBAtlas::sync_domain(Domain domain, const Rect &rect)
{
	if (inside_render_pass(rect))
		flush_render_pass();

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	// If we need to see a "clean" version
	// of a framebuffer domain, we need to see
	// anything other than this flag.
	unsigned dirty_bits = 1u << (domain == Domain::Unscaled ? STATUS_SFB_ONLY : STATUS_FB_ONLY);
	unsigned bits = 0;

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			bits |= 1u << (info(x, y) & STATUS_OWNERSHIP_MASK);

	unsigned write_domains = 0;

	// We're asserting that a region is up to date, but it's
	// not, so we have to resolve it.
	if ((bits & dirty_bits) == 0)
		return;

	// For scaled domain,
	// we need to blit from unscaled domain to scaled.
	unsigned ownership;
	unsigned hazard_domains;
	unsigned resolve_domains;
	if (domain == Domain::Scaled)
	{
		ownership = STATUS_FB_ONLY;
		hazard_domains = STATUS_FB_WRITE | STATUS_SFB_WRITE | STATUS_SFB_READ;

		//resolve_domains = STATUS_TRANSFER_FB_READ | STATUS_FB_PREFER | STATUS_TRANSFER_SFB_WRITE;
		resolve_domains = STATUS_COMPUTE_FB_READ | STATUS_FB_PREFER | STATUS_COMPUTE_SFB_WRITE;
	}
	else
	{
		ownership = STATUS_SFB_ONLY;
		hazard_domains = STATUS_FB_WRITE | STATUS_SFB_WRITE | STATUS_FB_READ;

		//resolve_domains = STATUS_TRANSFER_SFB_READ | STATUS_SFB_PREFER | STATUS_TRANSFER_FB_WRITE;
		resolve_domains = STATUS_COMPUTE_SFB_READ | STATUS_SFB_PREFER | STATUS_COMPUTE_FB_WRITE;
	}

	for (unsigned y = ybegin; y <= yend; y++)
	{
		for (unsigned x = xbegin; x <= xend; x++)
		{
			auto &mask = info(x, y);
			// If our block isn't in the ownership class we want,
			// we need to read from one block and write to the other.
			// We might have to wait for writers on read,
			// and add hazard masks for our writes
			// so other readers can wait for us.
			if ((mask & STATUS_OWNERSHIP_MASK) == ownership)
				write_domains |= mask & hazard_domains;
		}
	}

	// If we hit any hazard, resolve it.
	if (write_domains)
		pipeline_barrier(write_domains);

	for (unsigned y = ybegin; y <= yend; y++)
	{
		for (unsigned x = xbegin; x <= xend; x++)
		{
			auto &mask = info(x, y);
			if ((mask & STATUS_OWNERSHIP_MASK) == ownership)
			{
				mask &= ~STATUS_OWNERSHIP_MASK;
				mask |= resolve_domains;
				listener->resolve(domain, BLOCK_WIDTH * x, BLOCK_HEIGHT * y);
			}
		}
	}
}

Domain FBAtlas::find_suitable_domain(const Rect &rect)
{
	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	for (unsigned y = ybegin; y <= yend; y++)
	{
		for (unsigned x = xbegin; x <= xend; x++)
		{
			unsigned i = info(x, y);
			if (i == STATUS_FB_ONLY || i == STATUS_FB_PREFER)
				return Domain::Unscaled;
		}
	}
	return Domain::Scaled;
}

bool FBAtlas::inside_render_pass(const Rect &rect)
{
	if (!renderpass.inside)
		return false;

	unsigned xbegin = rect.x & ~(BLOCK_WIDTH - 1);
	unsigned ybegin = rect.y & ~(BLOCK_HEIGHT - 1);
	unsigned xend = ((rect.x + rect.width - 1) | (BLOCK_WIDTH - 1)) + 1;
	unsigned yend = ((rect.y + rect.height - 1) | (BLOCK_HEIGHT - 1)) + 1;

	unsigned x0 = max(renderpass.rect.x, xbegin);
	unsigned x1 = min(renderpass.rect.x + renderpass.rect.width, xend);
	unsigned y0 = max(renderpass.rect.y, ybegin);
	unsigned y1 = min(renderpass.rect.y + renderpass.rect.height, yend);

	return x1 > x0 && y1 > y0;
}

void FBAtlas::flush_render_pass()
{
	if (!renderpass.inside)
		return;

	renderpass.inside = false;
	write_domain(Domain::Scaled, Stage::Fragment, renderpass.rect);
	listener->flush_render_pass(renderpass.rect);
}

void FBAtlas::set_texture_window(const Rect &rect)
{
	renderpass.texture_window = rect;
}

void FBAtlas::write_fragment()
{
	bool reads_window = renderpass.texture_mode != TextureMode::None;
	if (reads_window)
	{
		Rect shifted = renderpass.texture_window;
		bool reads_palette;
		switch (renderpass.texture_mode)
		{
		case TextureMode::Palette4bpp:
			shifted.x >>= 2;
			shifted.width >>= 2;
			reads_palette = true;
			break;

		case TextureMode::Palette8bpp:
			shifted.x >>= 1;
			shifted.width >>= 1;
			reads_palette = true;
			break;

		default:
			reads_palette = false;
			break;
		}
		shifted.x += renderpass.texture_offset_x;
		shifted.y += renderpass.texture_offset_y;

		const Rect palette_rect = { renderpass.palette_offset_x, renderpass.palette_offset_y,
			                        renderpass.texture_mode == TextureMode::Palette8bpp ? 256u : 16u, 1 };

		if (reads_palette)
		{
			if (inside_render_pass(shifted) || inside_render_pass(palette_rect))
				flush_render_pass();
		}
		else if (inside_render_pass(shifted))
			flush_render_pass();

		read_texture();
	}

	if (!renderpass.inside)
	{
		sync_domain(Domain::Scaled, renderpass.rect);
		renderpass.inside = true;
		renderpass.clean_clear = false;
	}
}

void FBAtlas::clear_rect(const Rect &rect, FBColor color)
{
	if (renderpass.rect == rect)
	{
		sync_domain(Domain::Scaled, rect);

		discard_render_pass();
		renderpass.inside = true;
		renderpass.clean_clear = true;
		renderpass.color = color;
	}
	else if (!renderpass.inside)
	{
		sync_domain(Domain::Scaled, rect);
		renderpass.inside = true;
		renderpass.clean_clear = false;
		listener->clear_quad(rect, color);
	}
	else
	{
		// TODO: If clear rect is outside the render region, flush the render pass.
		listener->clear_quad(rect, color);
	}
}

void FBAtlas::set_draw_rect(const Rect &rect)
{
	if (!renderpass.inside)
		renderpass.rect = rect;
	else if (renderpass.rect != rect)
	{
		flush_render_pass();
		renderpass.rect = rect;
	}
}

void FBAtlas::discard_render_pass()
{
	renderpass.inside = false;
	listener->discard_render_pass();
}

void FBAtlas::notify_external_barrier(StatusFlags domains)
{
	static const StatusFlags compute_read_stages = STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_SFB_READ;
	static const StatusFlags compute_write_stages = STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_WRITE;
	static const StatusFlags transfer_read_stages = STATUS_TRANSFER_FB_READ | STATUS_TRANSFER_SFB_READ;
	static const StatusFlags transfer_write_stages = STATUS_TRANSFER_FB_WRITE | STATUS_TRANSFER_SFB_WRITE;
	static const StatusFlags fragment_write_stages = STATUS_FRAGMENT_SFB_WRITE | STATUS_FRAGMENT_FB_WRITE;
	static const StatusFlags fragment_read_stages = STATUS_FRAGMENT_SFB_READ | STATUS_FRAGMENT_FB_READ;

	if (domains & compute_write_stages)
		domains |= compute_write_stages | compute_read_stages;
	if (domains & compute_read_stages)
		domains |= compute_read_stages;
	if (domains & transfer_write_stages)
		domains |= transfer_write_stages | transfer_read_stages;
	if (domains & transfer_read_stages)
		domains |= transfer_read_stages;
	if (domains & fragment_write_stages)
		domains |= fragment_write_stages | fragment_read_stages;
	if (domains & fragment_read_stages)
		domains |= fragment_read_stages;

	for (auto &f : fb_info)
		f &= ~domains;
}

void FBAtlas::pipeline_barrier(StatusFlags domains)
{
	listener->hazard(domains);
	notify_external_barrier(domains);
}
}
