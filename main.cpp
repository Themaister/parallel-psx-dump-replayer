#include "device.hpp"
#include "renderer/renderer.hpp"
#include "wsi.hpp"
#include <cmath>
#include <random>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <renderer.hpp>
#include "stb_image_write.h"

using namespace PSX;
using namespace std;
using namespace Vulkan;

enum
{
   RSX_END = 0,
   RSX_PREPARE_FRAME,
   RSX_FINALIZE_FRAME,
   RSX_TEX_WINDOW,
   RSX_MASK_SETTING,
   RSX_DRAW_OFFSET,
   RSX_DRAW_AREA,
   RSX_DISPLAY_MODE,
   RSX_TRIANGLE,
   RSX_QUAD,
   RSX_LINE,
   RSX_LOAD_IMAGE,
   RSX_FILL_RECT,
   RSX_COPY_RECT,
   RSX_TOGGLE_DISPLAY
};

static void read_tag(FILE *file)
{
	char buffer[8];
	if (fread(buffer, sizeof(buffer), 1, file) != 1)
		throw runtime_error("Failed to read tag.");
	if (memcmp(buffer, "RSXDUMP1", sizeof(buffer)))
		throw runtime_error("Failed to read tag.");
}

static uint32_t read_u32(FILE *file)
{
	uint32_t val;
	if (fread(&val, sizeof(val), 1, file) != 1)
		throw runtime_error("Failed to read u32");
	return val;
}

static int32_t read_i32(FILE *file)
{
	int32_t val;
	if (fread(&val, sizeof(val), 1, file) != 1)
		throw runtime_error("Failed to read i32");
	return val;
}

static int32_t read_f32(FILE *file)
{
	float val;
	if (fread(&val, sizeof(val), 1, file) != 1)
		throw runtime_error("Failed to read f32");
	return val;
}

struct CommandVertex
{
	float x, y, w;
	uint32_t color;
	uint8_t tx, ty;
};

struct RenderState
{
	uint16_t texpage_x, texpage_y;
	uint16_t clut_x, clut_y;
	uint8_t texture_blend_mode;
	uint8_t depth_shift;
	bool dither;
	uint32_t blend_mode;
};

CommandVertex read_vertex(FILE *file)
{
	CommandVertex buf = {};
	buf.x = read_f32(file);
	buf.y = read_f32(file);
	buf.w = read_f32(file);
	buf.color = read_u32(file);
	buf.tx = uint8_t(read_u32(file));
	buf.ty = uint8_t(read_u32(file));
	return buf;
}

RenderState read_state(FILE *file)
{
	RenderState state = {};
	state.texpage_x = read_u32(file);
	state.texpage_y = read_u32(file);
	state.clut_x = read_u32(file);
	state.clut_y = read_u32(file);
	state.texture_blend_mode = read_u32(file);
	state.depth_shift = read_u32(file);
	state.dither = read_u32(file) != 0;
	state.blend_mode = read_u32(file);
	return state;
}

struct CommandLine
{
	int16_t x0, y0, x1, y1;
	uint32_t c0, c1;
	bool dither;
	uint32_t blend_mode;
};

CommandLine read_line(FILE *file)
{
	CommandLine line = {};
	line.x0 = read_i32(file);
	line.y0 = read_i32(file);
	line.x1 = read_i32(file);
	line.y1 = read_i32(file);
	line.c0 = read_u32(file);
	line.c1 = read_u32(file);
	line.dither = read_u32(file) != 0;
	line.blend_mode = read_u32(file);
	return line;
}

static void log_vertex(const CommandVertex &v)
{
	fprintf(stderr, "  x = %.1f, y = %.1f, w = %.1f, c = 0x%x, u = %u, v = %u\n",
            v.x, v.y, v.w, v.color, v.tx, v.ty);
}

static void log_state(const RenderState &s)
{
	fprintf(stderr, " Page = (%u, %u), CLUT = (%u, %u), texture_blend_mode = %u, depth_shift = %u, dither = %s, blend_mode = %u\n",
            s.texpage_x, s.texpage_y,
            s.clut_x, s.clut_y,
            s.texture_blend_mode, s.depth_shift, s.dither ? "on" : "off", s.blend_mode);
}

static void set_renderer_state(Renderer &renderer, const RenderState &state)
{
	renderer.set_texture_color_modulate(state.texture_blend_mode == 2);
	renderer.set_palette_offset(state.clut_x, state.clut_y);
	renderer.set_texture_offset(state.texpage_x, state.texpage_y);
	if (state.texture_blend_mode != 0)
	{
		switch (state.depth_shift)
		{
		default:
		case 0:
			renderer.set_texture_mode(TextureMode::ABGR1555);
			break;
		case 1:
			renderer.set_texture_mode(TextureMode::Palette8bpp);
			break;
		case 2:
			renderer.set_texture_mode(TextureMode::Palette4bpp);
			break;
		}

		switch (state.blend_mode)
		{
		default:
			renderer.set_semi_transparent(SemiTransparentMode::None);
			break;

		case 0:
			renderer.set_semi_transparent(SemiTransparentMode::Average);
			break;
		case 1:
			renderer.set_semi_transparent(SemiTransparentMode::Add);
			break;
		case 2:
			renderer.set_semi_transparent(SemiTransparentMode::Sub);
			break;
		case 3:
			renderer.set_semi_transparent(SemiTransparentMode::AddQuarter);
			break;
		}
	}
	else
	{
		renderer.set_texture_mode(TextureMode::None);
		renderer.set_semi_transparent(SemiTransparentMode::None);
	}
}

static bool read_command(FILE *file, Renderer &renderer, bool &eof)
{
	auto op = read_u32(file);
	eof = false;
	switch (op)
	{
	case RSX_PREPARE_FRAME:
		break;
	case RSX_FINALIZE_FRAME:
		return false;
	case RSX_END:
		eof = true;
		return false;

	case RSX_TEX_WINDOW:
	{
		auto tww = read_u32(file);
		auto twh = read_u32(file);
		auto twx = read_u32(file);
		auto twy = read_u32(file);

		auto tex_x_mask = ~(tww << 3);
		auto tex_x_or = (twx & tww) << 3;
		auto tex_y_mask = ~(twh << 3);
		auto tex_y_or = (twy & twh) << 3;

		auto width = 1 << (32 - leading_zeroes(tex_x_mask & 0xff));
		auto height = 1 << (32 - leading_zeroes(tex_y_mask & 0xff));
		VK_ASSERT(width <= 256);
		VK_ASSERT(height <= 256);
		renderer.set_texture_window({ tex_x_or, tex_y_or, width, height });

		break;
	}

	case RSX_MASK_SETTING:
	{
		auto mask_set_or = read_u32(file);
		auto mask_eval_and = read_u32(file);
		break;
	}

	case RSX_DRAW_OFFSET:
	{
		auto x = read_i32(file);
		auto y = read_i32(file);

		renderer.set_draw_offset(x, y);
		break;
	}

	case RSX_DRAW_AREA:
	{
		auto x0 = read_u32(file);
		auto y0 = read_u32(file);
		auto x1 = read_u32(file);
		auto y1 = read_u32(file);

		int width = x1 - x0 + 1;
		int height = y1 - y0 + 1;
		width = max(width, 0);
		height = max(height, 0);

		width = min(width, int(FB_WIDTH - x0));
		height = min(height, int(FB_HEIGHT - y0));
		renderer.set_draw_rect({ x0, y0, unsigned(width), unsigned(height) });
		break;
	}

	case RSX_DISPLAY_MODE:
	{
		auto x = read_u32(file);
		auto y = read_u32(file);
		auto w = read_u32(file);
		auto h = read_u32(file);
		auto depth_24bpp = read_u32(file);

		renderer.set_display_mode({ x, y, w, h }, depth_24bpp != 0);
		break;
	}

	case RSX_TRIANGLE:
	{
		auto v0 = read_vertex(file);
		auto v1 = read_vertex(file);
		auto v2 = read_vertex(file);
		auto state = read_state(file);

		Vertex vertices[3] = {
			{ v0.x, v0.y, v0.w, v0.color, v0.tx, v0.ty },
			{ v1.x, v1.y, v1.w, v1.color, v1.tx, v1.ty },
			{ v2.x, v2.y, v2.w, v2.color, v2.tx, v2.ty },
		};

		set_renderer_state(renderer, state);
		renderer.draw_triangle(vertices);
		break;
	}

	case RSX_QUAD:
	{
		auto v0 = read_vertex(file);
		auto v1 = read_vertex(file);
		auto v2 = read_vertex(file);
		auto v3 = read_vertex(file);
		auto state = read_state(file);

		Vertex vertices[4] = {
			{ v0.x, v0.y, v0.w, v0.color, v0.tx, v0.ty },
			{ v1.x, v1.y, v1.w, v1.color, v1.tx, v1.ty },
			{ v2.x, v2.y, v2.w, v2.color, v2.tx, v2.ty },
			{ v3.x, v3.y, v3.w, v3.color, v3.tx, v3.ty },
		};

		set_renderer_state(renderer, state);
		renderer.draw_quad(vertices);
		break;
	}

	case RSX_LINE:
		read_line(file);
		break;

	case RSX_LOAD_IMAGE:
	{
		auto x = read_u32(file);
		auto y = read_u32(file);
		auto width = read_u32(file);
		auto height = read_u32(file);
		vector<uint16_t> tmp(width * height);
		fread(tmp.data(), sizeof(uint16_t), width * height, file);

		VK_ASSERT(width * height <= 0x10000);

		renderer.copy_cpu_to_vram(tmp.data(), { x, y, width, height });
		break;
	}

	case RSX_FILL_RECT:
	{
		auto color = read_u32(file);
		auto x = read_u32(file);
		auto y = read_u32(file);
		auto w = read_u32(file);
		auto h = read_u32(file);
		renderer.clear_rect({ x, y, w, h }, color);
		break;
	}

	case RSX_COPY_RECT:
	{
		auto src_x = read_u32(file);
		auto src_y = read_u32(file);
		auto dst_x = read_u32(file);
		auto dst_y = read_u32(file);
		auto w = read_u32(file);
		auto h = read_u32(file);
		renderer.blit_vram({ dst_x, dst_y, w, h }, { src_x, src_y, w, h });
		break;
	}

	case RSX_TOGGLE_DISPLAY:
	{
		auto toggle = read_u32(file);
		renderer.toggle_display(toggle == 0);
		break;
	}

	default:
		throw runtime_error("Invalid opcode.");
	}
	return true;
}

static double gettime()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + 1e-9 * ts.tv_nsec;
}

static void dump_to_file(Device &device, Renderer &renderer, unsigned index)
{
	unsigned width, height;
	auto buffer = renderer.scanout_to_buffer(width, height);
	if (!buffer)
		return;

	char path[1024];
	snprintf(path, sizeof(path), "/tmp/test-%06u.bmp", index);

	device.wait_idle();
	uint32_t *data = static_cast<uint32_t *>(device.map_host_buffer(*buffer, MaliSDK::MEMORY_ACCESS_READ));
	for (unsigned i = 0; i < width * height; i++)
		data[i] |= 0xff000000u;

	if (!stbi_write_bmp(path, width, height, 4, data))
		LOG("Failed to write image.");
	device.unmap_host_buffer(*buffer);
}

int main()
{
	WSI wsi;
	wsi.init(1280, 720);
	auto &device = wsi.get_device();
	Renderer renderer(device, 1);

	FILE *file = fopen("/tmp/crash.rsx", "rb");
	if (!file)
		return 1;

	read_tag(file);

	bool eof = false;
	unsigned frames = 0;
	double total_time = 0.0;
	while (!eof && wsi.alive())
	{
		double start = gettime();
		wsi.begin_frame();
		renderer.reset_counters();
		while (read_command(file, renderer, eof));
		renderer.scanout();

		dump_to_file(device, renderer, frames);
		wsi.end_frame();
		double end = gettime();
		total_time += end - start;
		frames++;

		LOG("Render passes: %u\n", renderer.counters.render_passes);
		LOG("Draw calls: %u\n", renderer.counters.draw_calls);
		LOG("Texture flushes: %u\n", renderer.counters.texture_flushes);
		LOG("Vertices: %u\n", renderer.counters.vertices);
	}

	LOG("Ran %u frames in %f s! (%.3f ms / frame).\n", frames, total_time, 1000.0 * total_time / frames);
}
