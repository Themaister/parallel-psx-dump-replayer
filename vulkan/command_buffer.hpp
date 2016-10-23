#pragma once

#include "buffer.hpp"
#include "image.hpp"
#include "intrusive.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "shader.hpp"
#include "vulkan.hpp"

namespace Vulkan
{

enum CommandBufferDirtyBits
{
	COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT = 1 << 0,
	COMMAND_BUFFER_DIRTY_PIPELINE_BIT = 1 << 1,

	COMMAND_BUFFER_DIRTY_VIEWPORT_BIT = 1 << 2,
	COMMAND_BUFFER_DIRTY_SCISSOR_BIT = 1 << 3,

	COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT = 1 << 4,

	COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT = 1 << 6,

	COMMAND_BUFFER_DYNAMIC_BITS = COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | COMMAND_BUFFER_DIRTY_SCISSOR_BIT
};
using CommandBufferDirtyFlags = uint32_t;

class Device;
class CommandBuffer : public IntrusivePtrEnabled<CommandBuffer>
{
public:
	CommandBuffer(Device *device, VkCommandBuffer cmd, VkPipelineCache cache);
	VkCommandBuffer get_command_buffer()
	{
		return cmd;
	}

	bool swapchain_touched() const
	{
		return uses_swapchain;
	}

	void copy_buffer(const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
	                 VkDeviceSize size);
	void copy_buffer(const Buffer &dst, const Buffer &src);

	void copy_buffer_to_image(const Image &image, const Buffer &buffer, VkDeviceSize buffer_offset,
	                          const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
	                          unsigned slice_height, const VkImageSubresourceLayers &subresrouce);

	void copy_image_to_buffer(const Buffer &dst, const Image &src, VkDeviceSize buffer_offset, const VkOffset3D &offset,
	                          const VkExtent3D &extent, unsigned row_length, unsigned slice_height,
	                          const VkImageSubresourceLayers &subresrouce);

	void barrier(VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	             VkAccessFlags dst_access);

	void buffer_barrier(const Buffer &buffer, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
	                    VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);

	void image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
	                   VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	                   VkAccessFlags dst_access);
	void image_barrier(const Image &image, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
	                   VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);

	void blit_image(const Image &dst, const Image &src, const VkOffset3D &dst_offset, const VkOffset3D &dst_extent,
	                const VkOffset3D &src_offset, const VkOffset3D &src_extent, unsigned dst_level, unsigned src_level,
	                unsigned dst_base_layer = 0, uint32_t src_base_layer = 0, unsigned num_layers = 1,
	                VkFilter filter = VK_FILTER_LINEAR);

	void begin_render_pass(const RenderPassInfo &info);
	void end_render_pass(VkPipelineStageFlags color_access_stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
	                                                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                     VkAccessFlags color_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
	                                                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
	                     VkPipelineStageFlags depth_stencil_access_stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
	                                                                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
	                                                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
	                     VkAccessFlags depth_stencil_access = VK_ACCESS_SHADER_READ_BIT |
	                                                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
	                                                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

	void bind_program(Program &program);
	void set_texture(unsigned set, unsigned binding, const ImageView &view);
	void set_texture(unsigned set, unsigned binding, const ImageView &view, const Sampler &sampler);
	void set_texture(unsigned set, unsigned binding, const ImageView &view, StockSampler sampler);
	void set_storage_texture(unsigned set, unsigned binding, const ImageView &view);
	void set_sampler(unsigned set, unsigned binding, const Sampler &sampler);
	void set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer);
	void set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
	                        VkDeviceSize range);
	void set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer);
	void set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
	                        VkDeviceSize range);
	void push_constants(const void *data, VkDeviceSize offset, VkDeviceSize range);

	void set_viewport(const VkViewport &viewport);
	void set_scissor(const VkRect2D &rect);

	void set_vertex_attrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset);
	void set_vertex_binding(uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
	                        VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);
	void bind_index_buffer(const Buffer &buffer, VkDeviceSize offset, VkIndexType index_type);

	void draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0, uint32_t first_instance = 0);
	void draw_indexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0, int32_t vertex_offset = 0, uint32_t first_instance = 0);

	void dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);

private:
	Device *device;
	VkCommandBuffer cmd;
	VkPipelineCache cache;

	const Framebuffer *framebuffer = nullptr;
	const RenderPass *render_pass = nullptr;
	RenderPassInfo render_pass_info;

	struct AttribState
	{
		uint32_t binding;
		VkFormat format;
		VkDeviceSize offset;
	};
	AttribState attribs[VULKAN_NUM_VERTEX_ATTRIBS] = {};

	VkBuffer vbo_buffers[VULKAN_NUM_VERTEX_BUFFERS] = {};
	VkDeviceSize vbo_offsets[VULKAN_NUM_VERTEX_BUFFERS] = {};
	VkDeviceSize vbo_strides[VULKAN_NUM_VERTEX_BUFFERS] = {};
	VkVertexInputRate vbo_input_rates[VULKAN_NUM_VERTEX_BUFFERS] = {};

	struct Binding
	{
		union {
			VkDescriptorBufferInfo buffer;
			VkDescriptorImageInfo image;
		};
	};
	Binding bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};
	uint64_t cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};
	uint64_t secondary_cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};
	uint8_t push_constant_data[VULKAN_PUSH_CONSTANT_SIZE] = {};

	struct IndexState
	{
		VkBuffer buffer;
		VkDeviceSize offset;
		VkIndexType index_type;
	};
	IndexState index = {};

	VkPipeline current_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout current_pipeline_layout = VK_NULL_HANDLE;
	PipelineLayout *current_layout = nullptr;
	Program *current_program = nullptr;

	VkViewport viewport = {};
	VkRect2D scissor = {};

	CommandBufferDirtyFlags dirty = ~0u;
	uint32_t dirty_sets = 0;
	uint32_t dirty_vbos = 0;
	uint32_t active_vbos = 0;
	bool uses_swapchain = false;
	bool is_compute = true;

	void set_dirty(CommandBufferDirtyFlags flags)
	{
		dirty |= flags;
	}

	CommandBufferDirtyFlags get_and_clear(CommandBufferDirtyFlags flags)
	{
		auto mask = dirty & flags;
		dirty &= ~flags;
		return mask;
	}

	union PipelineState {
		struct
		{
			uint32_t state;
		} state;
		uint64_t words = 0;
	};
	PipelineState static_state = {};

	void flush_render_state();
	VkPipeline build_graphics_pipeline(Hash hash);
	void flush_graphics_pipeline();
	void flush_descriptor_sets();
	void begin_graphics();
	void flush_descriptor_set(uint32_t set);
	void begin_compute();
	void begin_context();

	void flush_compute_state();
};

using CommandBufferHandle = IntrusivePtr<CommandBuffer>;
}
