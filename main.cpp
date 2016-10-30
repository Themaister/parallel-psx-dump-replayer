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

int main()
{
	WSI wsi;
	wsi.init(1280, 720);

	auto &device = wsi.get_device();
	Renderer renderer(device, 4);

	const Vertex verts[3] = {
		{ 26.0f, 26.0f, 1.0f, 0x00ffffff }, { 42.0f, 43.0f, 1.0f, 0x862010ff }, { 22.0f, 36.0f, 1.0f, 0xa5405060 },
	};

	const Vertex verts2[4] = {
		{ 30.0f, 20.0f, 1.0f, 0x00ffffff },
		{ 40.0f, 20.0f, 1.0f, 0x862010ff },
		{ 30.0f, 30.0f, 1.0f, 0xa5405060 },
		{ 40.0f, 30.0f, 1.0f, 0xa5405060 },
	};

	const Vertex verts3[3] = {
		{ 60.0f, 60.0f, 1.0f, 0xffffffff, 0, 0 },
		{ 80.0f, 60.0f, 1.0f, 0xffffffff, 16, 0 },
		{ 60.0f, 80.0f, 1.0f, 0xffffffff, 0, 16 },
	};

	const Vertex verts4[4] = {
		{ 40.0f, 40.0f, 1.0f, 0xffffffff, 0, 0 },
		{ 60.0f, 40.0f, 1.0f, 0xffffffff, 8, 0 },
		{ 40.0f, 60.0f, 1.0f, 0xffffffff, 0, 8},
		{ 60.0f, 60.0f, 1.0f, 0xffffffff, 8, 8 },
	};

	uint16_t black[16 * 16];
	for (auto &l : black)
		l = 0xaaaa;
	for (unsigned i = 0; i < 4; i++)
		black[i] = 0;

	uint16_t palentry[4] = { 0x8000, 31 << 0, 31 << 5, 31 << 10 };
	uint16_t paltexture[4 * 8] = {
		0x0100, 0x0101, 0x0202, 0x0303,
		0x0201, 0x0101, 0x0202, 0x0303,
		0x0100, 0x0101, 0x0202, 0x0303,
		0x0201, 0x0101, 0x0202, 0x0303,
		0x0100, 0x0101, 0x0202, 0x0303,
		0x0201, 0x0101, 0x0202, 0x0303,
		0x0100, 0x0101, 0x0202, 0x0303,
		0x0201, 0x0101, 0x0202, 0x0303,
	};

	while (!wsi.alive())
	{
		wsi.begin_frame();
		renderer.set_texture_mode(TextureMode::None);
		renderer.set_draw_rect({ 24, 24, 49, 49 });
		renderer.clear_rect({ 24, 24, 49, 49 }, 0x38ac);
		renderer.draw_triangle(verts);
		renderer.draw_quad(verts2);

		renderer.set_texture_offset(24, 24);
		renderer.set_texture_window({0, 0, 16, 16});
		renderer.set_texture_mode(TextureMode::ABGR1555);
		renderer.draw_triangle(verts3);

		renderer.copy_cpu_to_vram(palentry, { 512, 0, 4, 1 });
		renderer.copy_cpu_to_vram(paltexture, { 512, 16, 4, 8 });
		renderer.set_texture_offset(512 - 8, 16 - 16);
		renderer.set_palette_offset(512, 0);
		renderer.set_texture_window({16, 16, 8, 8});
		renderer.set_texture_mode(TextureMode::Palette8bpp);
		renderer.set_semi_transparent(true);
		renderer.draw_quad(verts4);
		renderer.set_semi_transparent(false);

		renderer.scanout({ 0, 0, 128, 72 });
		wsi.end_frame();
	}
}
