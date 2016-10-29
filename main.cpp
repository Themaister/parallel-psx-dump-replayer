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

	Vertex verts[3] = {
		{ 26.0f, 26.0f, 1.0f, 0x00ffffff }, { 42.0f, 43.0f, 1.0f, 0x862010ff }, { 22.0f, 36.0f, 1.0f, 0xa5405060 },
	};

	Vertex verts2[4] = {
		{ 30.0f, 20.0f, 1.0f, 0x00ffffff },
		{ 40.0f, 20.0f, 1.0f, 0x862010ff },
		{ 30.0f, 30.0f, 1.0f, 0xa5405060 },
		{ 40.0f, 30.0f, 1.0f, 0xa5405060 },
	};

	uint16_t black[16 * 16];
	for (auto &l : black)
		l = 0xffff;

	while (!wsi.alive())
	{
		wsi.begin_frame();
		renderer.set_draw_rect({ 24, 24, 49, 49 });
		renderer.clear_rect({ 24, 24, 49, 49 }, 0x5555);
		renderer.draw_triangle(verts);
		renderer.clear_rect({ 30, 28, 7, 7 }, 0x8882);
		renderer.draw_quad(verts2);
		renderer.copy_cpu_to_vram(black, { 32, 32, 5, 8 });
		renderer.scanout({ 0, 0, 80, 60 });
		wsi.end_frame();
	}
}
