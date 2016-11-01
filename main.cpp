#include "device.hpp"
#include "renderer/renderer.hpp"
#include "wsi.hpp"
#include <cmath>
#include <random>
#include <stdio.h>
#include <string.h>
#include <vector>

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
	uint32_t tx, ty;
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
	buf.tx = read_u32(file);
	buf.ty = read_u32(file);
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
		fprintf(stderr, "TEX WINDOW\n");
		auto tww = read_u32(file);
		auto twh = read_u32(file);
		auto twx = read_u32(file);
		auto twy = read_u32(file);
		fprintf(stderr, "  tww = %u, twh = %u, twx = %u, twy = %u\n", tww, twh, twx, twy);
		break;
	}

	case RSX_MASK_SETTING:
	{
		fprintf(stderr, "MASK SETTING\n");
		auto mask_set_or = read_u32(file);
		auto mask_eval_and = read_u32(file);
		fprintf(stderr, "  mask_set_or = %u, mask_eval_and = %u\n", mask_set_or, mask_eval_and);
		break;
	}

	case RSX_DRAW_OFFSET:
	{
		fprintf(stderr, "DRAW OFFSET\n");
		auto x = read_i32(file);
		auto y = read_i32(file);
		fprintf(stderr, "  x = %d, y = %d\n", x, y);
		break;
	}

	case RSX_DRAW_AREA:
	{
		fprintf(stderr, "DRAW AREA\n");
		auto x0 = read_u32(file);
		auto y0 = read_u32(file);
		auto x1 = read_u32(file);
		auto y1 = read_u32(file);
		fprintf(stderr, "  x0 = %u, y0 = %u, x1 = %u, y1 = %u\n", x0, y0, x1, y1);
		break;
	}

	case RSX_DISPLAY_MODE:
	{
		fprintf(stderr, "DISPLAY MODE\n");
		auto x = read_u32(file);
		auto y = read_u32(file);
		auto w = read_u32(file);
		auto h = read_u32(file);
		auto depth_24bpp = read_u32(file);
		fprintf(stderr, "  x = %u, y = %u, w = %u, h = %u, 24-bit %s\n", x, y, w, h, depth_24bpp ? "on" : "off");
		break;
	}

	case RSX_TRIANGLE:
	{
		fprintf(stderr, "TRIANGLE\n");
		auto v0 = read_vertex(file);
		auto v1 = read_vertex(file);
		auto v2 = read_vertex(file);
		auto state = read_state(file);
		log_vertex(v0);
		log_vertex(v1);
		log_vertex(v2);
		log_state(state);
		break;
	}

	case RSX_QUAD:
	{
		fprintf(stderr, "QUAD\n");
		auto v0 = read_vertex(file);
		auto v1 = read_vertex(file);
		auto v2 = read_vertex(file);
		auto v3 = read_vertex(file);
		auto state = read_state(file);
		log_vertex(v0);
		log_vertex(v1);
		log_vertex(v2);
		log_vertex(v3);
		log_state(state);
		break;
	}

	case RSX_LINE:
		fprintf(stderr, "LINE\n");
		read_line(file);
		break;

	case RSX_LOAD_IMAGE:
	{
		fprintf(stderr, "LOAD IMAGE\n");
		auto x = read_u32(file);
		auto y = read_u32(file);
		auto width = read_u32(file);
		auto height = read_u32(file);
		fprintf(stderr, "  x = %u, y = %u, w = %u, h = %u\n", x, y, width, height);
		fseek(file, width * height * sizeof(uint16_t), SEEK_CUR);
		break;
	}

	case RSX_FILL_RECT:
	{
		fprintf(stderr, "FILL RECT\n");
		auto color = read_u32(file);
		auto x = read_u32(file);
		auto y = read_u32(file);
		auto w = read_u32(file);
		auto h = read_u32(file);
		fprintf(stderr, "  color = 0x%x, x = %u, y = %u, w = %u, h = %u\n", color, x, y, w, h);
		break;
	}

	case RSX_COPY_RECT:
	{
		fprintf(stderr, "COPY RECT\n");
		auto src_x = read_u32(file);
		auto src_y = read_u32(file);
		auto dst_x = read_u32(file);
		auto dst_y = read_u32(file);
		auto w = read_u32(file);
		auto h = read_u32(file);
		fprintf(stderr, "  srcx = %u, srcy = %u, dstx = %u, dsty = %u, w = %u, h = %u\n", src_x, src_y, dst_x, dst_y, w, h);
		break;
	}

	case RSX_TOGGLE_DISPLAY:
	{
		fprintf(stderr, "TOGGLE DISPLAY\n");
		auto toggle = read_u32(file);
		fprintf(stderr, "  Toggle %s\n", toggle ? "on" : "off");
		break;
	}

	default:
		throw runtime_error("Invalid opcode.");
	}
	return true;
}

int main()
{
	WSI wsi;
	wsi.init(1280, 720);
	auto &device = wsi.get_device();
	Renderer renderer(device, 4);

	FILE *file = fopen("/tmp/spyro.rsx", "rb");
	if (!file)
		return 1;

	read_tag(file);

	bool eof = false;
	while (!eof && wsi.alive())
	{
		wsi.begin_frame();
		while (read_command(file, renderer, eof));
		wsi.end_frame();
	}
}
