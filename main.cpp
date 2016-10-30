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

	const Vertex verts4[3] = {
		{ 40.0f, 40.0f, 1.0f, 0xffffffff, 0, 0 },
		{ 60.0f, 40.0f, 1.0f, 0xffffffff, 16, 0 },
		{ 40.0f, 60.0f, 1.0f, 0xffffffff, 0, 16 },
	};

	uint16_t black[16 * 16];
	for (auto &l : black)
		l = 0xaaaa;
	for (unsigned i = 0; i < 4; i++)
		black[i] = 0;

	while (!wsi.alive())
	{
		wsi.begin_frame();
		renderer.set_texture_format(TextureMode::None);
		renderer.set_draw_rect({ 24, 24, 49, 49 });
		renderer.clear_rect({ 24, 24, 49, 49 }, 0x38ac);
		renderer.draw_triangle(verts);
		renderer.draw_quad(verts2);

		renderer.copy_cpu_to_vram(black, {256, 256, 16, 16});

		renderer.set_texture_offset(24, 24);
		renderer.set_texture_window({0, 0, 16, 16});
		renderer.set_texture_format(TextureMode::ABGR1555);
		renderer.draw_triangle(verts3);
		renderer.set_texture_offset(256, 256);
		renderer.set_texture_window({0, 0, 16, 16});
		renderer.set_texture_format(TextureMode::ABGR1555);
		renderer.draw_triangle(verts4);

		renderer.scanout({ 0, 0, 128, 72 });
		wsi.end_frame();
	}
}
