#pragma once

#include "buffer.hpp"
#include "image.hpp"
#include "intrusive.hpp"
#include "sampler.hpp"
#include "shader.hpp"
#include "vulkan.hpp"

namespace Vulkan
{

enum CommandBufferDirtyBits
{
	COMMAND_BUFFER_DIRTY_STATIC = 1 << 0,
	COMMAND_BUFFER_DIRTY_PIPELINE_LAYOUT = 1 << 1,
	COMMAND_BUFFER_DIRTY_PIPELINE = 1 << 2,

	COMMAND_BUFFER_DIRTY_VIEWPORT = 1 << 3,
	COMMAND_BUFFER_DIRTY_SCISSOR = 1 << 4
};
using CommandBufferDirtyFlags = uint32_t;

class Device;
class CommandBuffer : public IntrusivePtrEnabled<CommandBuffer>
{
public:
	CommandBuffer(Device *device, VkCommandBuffer cmd);
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

	void bind_program(const Program &program);
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

private:
	Device *device;
	VkCommandBuffer cmd;

	struct Binding
	{
		union {
			struct
			{
				VkImageView view;
				VkSampler sampler;
			} image;

			struct
			{
				VkBuffer buffer;
				VkDeviceSize offset;
				VkDeviceSize range;
			} buffer;
		};
		uint64_t cookie;
	};
	Binding bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};

	VkPipeline current_pipeline = VK_NULL_HANDLE;
	PipelineLayout *current_layout = nullptr;
	VkDescriptorSet current_sets[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	const Program *current_program = nullptr;

	VkViewport viewport = {};
	VkRect2D scissor = {};

	CommandBufferDirtyFlags dirty = ~0u;
	uint32_t dirty_sets = 0;
	bool uses_swapchain = false;
	bool is_compute = false;
};

using CommandBufferHandle = IntrusivePtr<CommandBuffer>;
}
