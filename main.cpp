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

class Listener : public HazardListener
{
public:
	Listener();
	void hazard(StatusFlags flags) override;
	void resolve(Domain target_domain, const Rect &rect) override;
	void flush_render_pass() override;
	void discard_render_pass() override;
	void upload_texture(Domain target_domain, const Rect &rect) override;

	void copy_cpu_to_vram(const Rect &rect);
	void copy_vram_to_vram(const Rect &src, const Rect &dst);

	void write_fragment(bool reads_window);
	void clear_rect(const Rect &rect);
	void set_draw_rect(const Rect &rect);
	void set_texture_window(const Rect &rect);

	void flush();

private:
	FBAtlas atlas;

	struct CPUtoGPUCopy
	{
		Rect rect;
		unsigned offset;
	};

	struct GPUtoGPUCopy
	{
		Domain domain;
		Rect src;
		Rect dst;
	};

	struct ResolveJob
	{
		Domain domain;
		Rect rect;
	};

	struct TextureUpload
	{
		Domain domain;
		Rect rect;
	};

	enum class Primitive
	{
		FillRect,
		Triangle,
		Quad,
		Rect
	};

	struct DrawCall
	{
		Primitive prim;
	};

	vector<CPUtoGPUCopy> cpu_to_gpu;
	vector<GPUtoGPUCopy> gpu_to_gpu;
	vector<TextureUpload> gpu_to_texture;
	vector<ResolveJob> resolves;
	vector<DrawCall> draw_calls;
};

Listener::Listener()
{
	atlas.set_hazard_listener(this);
}

void Listener::flush()
{
	atlas.pipeline_barrier(STATUS_ALL);
}

void Listener::copy_cpu_to_vram(const Rect &rect)
{
	atlas.write_compute(Domain::Unscaled, rect);
	cpu_to_gpu.push_back({ rect, 0u });
}

void Listener::copy_vram_to_vram(const Rect &src, const Rect &dst)
{
	atlas.read_compute(Domain::Unscaled, src);
	atlas.write_compute(Domain::Unscaled, dst);
	gpu_to_gpu.push_back({ Domain::Unscaled, src, dst });
}

void Listener::upload_texture(Domain domain, const Rect &rect)
{
	gpu_to_texture.push_back({ domain, rect });
}

void Listener::write_fragment(bool reads_window)
{
	atlas.write_fragment(reads_window);
	draw_calls.push_back({ Primitive::Triangle });
}

void Listener::clear_rect(const Rect &rect)
{
	atlas.clear_rect(rect);
	draw_calls.push_back({ Primitive::FillRect });
}

void Listener::hazard(StatusFlags domains)
{
	unsigned compute_stages =
	    STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_READ | STATUS_COMPUTE_SFB_WRITE;

	unsigned transfer_stages =
	    STATUS_TRANSFER_FB_READ | STATUS_TRANSFER_SFB_READ | STATUS_TRANSFER_FB_WRITE | STATUS_TRANSFER_SFB_WRITE;

	unsigned fragment_stages = STATUS_FRAGMENT_SFB_WRITE;

	if (domains & compute_stages)
	{
		for (auto &call : cpu_to_gpu)
		{
			fprintf(stderr, "Flushing CPU to GPU copy (%u, %u, %u, %u).\n", call.rect.x, call.rect.y, call.rect.width,
			        call.rect.height);
		}

		for (auto &call : gpu_to_gpu)
		{
			fprintf(stderr, "Flushing GPU to GPU copy (%u, %u, %u, %u) -> (%u, %u, %u, %u).\n", call.src.x, call.src.y,
			        call.src.width, call.src.height, call.dst.x, call.dst.y, call.dst.width, call.dst.height);
		}

		for (auto &call : gpu_to_texture)
		{
			fprintf(stderr, "Flushing GPU to Texture copy (%u, %u, %u, %u).\n", call.rect.x, call.rect.y,
			        call.rect.width, call.rect.height);
		}
		cpu_to_gpu.clear();
		gpu_to_gpu.clear();
		gpu_to_texture.clear();

		fprintf(stderr, "=== COMPUTE BARRIER ===\n");
	}
	if (domains & transfer_stages)
	{
		for (auto &call : resolves)
		{
			fprintf(stderr, "Resolve %s (%u, %u, %u, %u).\n", call.domain == Domain::Scaled ? "Scaled" : "Unscaled",
			        call.rect.x, call.rect.y, call.rect.width, call.rect.height);
		}
		resolves.clear();

		fprintf(stderr, "=== TRANSFER BARRIER ===\n");
	}

	if (domains & fragment_stages)
	{
		fprintf(stderr, "=== FRAGMENT TO ALL BARRIER ===\n");
	}
}

void Listener::resolve(Domain target_domain, const Rect &rect)
{
	resolves.push_back({ target_domain, rect });
}

void Listener::flush_render_pass()
{
	bool is_clear = atlas.render_pass_is_clear();
	if (is_clear)
		fprintf(stderr, "Clear\n");
	else
		fprintf(stderr, "Readback\n");

	fprintf(stderr, "Flushing %zu draw calls.\n", draw_calls.size() - is_clear);
	fprintf(stderr, "=== FRAGMENT TO FRAGMENT BARRIER ===\n");
	draw_calls.clear();
}

void Listener::set_draw_rect(const Rect &rect)
{
	atlas.set_draw_rect(rect);
}

void Listener::set_texture_window(const Rect &rect)
{
	atlas.set_texture_window(rect);
}

void Listener::discard_render_pass()
{
	if (!draw_calls.empty())
		fprintf(stderr, "Discarding render pass.\n");
	draw_calls.clear();
}

int main()
{
	WSI wsi;
	wsi.init(1280, 720);

#if 0
	Listener listener;

	listener.copy_cpu_to_vram({ 0, 0, 8, 8 });
	listener.copy_vram_to_vram({ 0, 0, 8, 8 }, { 8, 8, 8, 8 });

	listener.set_draw_rect({ 64, 64, 256, 256 });
	listener.clear_rect({ 64, 64, 256, 256 });
	listener.set_texture_window({ 400, 400, 8, 8 });
	listener.write_fragment(true);

	listener.set_texture_window({ 128, 128, 8, 8 });
	listener.write_fragment(true);

	listener.copy_vram_to_vram({ 64, 64, 8, 8 }, { 8, 8, 8, 8 });
	listener.flush();
#endif

	auto &device = wsi.get_device();
	Renderer renderer(device, 4);

	static const uint32_t triangle_vert[] =
#include "triangle.vert.inc"
	    ;
	static const uint32_t triangle_frag[] =
#include "triangle.frag.inc"
	    ;

	static const uint32_t test_comp[] =
#include "test.comp.inc"
	    ;

	auto program = device.create_program(triangle_vert, sizeof(triangle_vert), triangle_frag, sizeof(triangle_frag));
	auto compute_program = device.create_program(test_comp, sizeof(test_comp));

	static const float vertex_data[6 * 4] = {
		-0.5f, 0.5f,  0.0f, 1.0f, 0.0f, 0.0f, +0.5f, 0.5f,  0.0f, 1.0f, 1.0f, 0.0f,
		-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, +0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 1.0f,
	};

	static const float vertex_data2[6 * 4] = {
		-0.2f, 0.2f,  0.0f, 1.0f, 0.0f, 0.0f, +0.2f, 0.2f,  0.0f, 1.0f, 1.0f, 0.0f,
		-0.2f, -0.2f, 0.0f, 1.0f, 0.0f, 1.0f, +0.2f, -0.2f, 0.0f, 1.0f, 1.0f, 1.0f,
	};

	static const uint16_t index_data[6] = {
		0, 1, 2, 3, 2, 1,
	};

	auto image_info = ImageCreateInfo::immutable_2d_image(16, 16, VK_FORMAT_R8G8B8A8_UNORM);
	image_info.misc = IMAGE_MISC_GENERATE_MIPS_BIT;
	image_info.levels = 0;
	uint32_t image_buffer[16 * 16];
	std::mt19937 mt(0);
	for (unsigned y = 0; y < 16; y++)
		for (unsigned x = 0; x < 16; x++)
			image_buffer[y * 16 + x] = mt();

	ImageInitialData initial = { image_buffer, 0, 0 };
	auto static_image = device.create_image(image_info, &initial);

	unsigned frame = 0;
	while (!wsi.alive())
	{
		wsi.begin_frame();
		renderer.scanout({ 0, 0, 32, 32 });
		wsi.end_frame();
	}
}
