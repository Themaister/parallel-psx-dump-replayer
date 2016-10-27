#include "device.hpp"
#include "wsi.hpp"
#include <cmath>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <random>
#include "renderer/renderer.hpp"

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
		{ 26.0f, 26.0f, 1.0f, 0x00ffffff },
		{ 42.0f, 43.0f, 1.0f, 0x862010ff },
		{ 22.0f, 36.0f, 1.0f, 0xa5405060 },
	};

	unsigned frame = 0;
	while (!wsi.alive())
	{
		wsi.begin_frame();
		renderer.set_draw_rect({ 24, 24, 18, 18 });
		renderer.clear_rect({ 24, 24, 18, 18 }, 0x5555);
		renderer.draw_triangle(verts);
		renderer.clear_rect({ 30, 28, 3, 3 }, 0x8882);
		renderer.scanout({ 0, 0, 320, 240 });
		wsi.end_frame();
	}
}
