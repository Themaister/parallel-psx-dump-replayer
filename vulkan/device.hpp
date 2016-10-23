#pragma once

#include "buffer.hpp"
#include "command_buffer.hpp"
#include "command_pool.hpp"
#include "fence_manager.hpp"
#include "hashmap.hpp"
#include "image.hpp"
#include "memory_allocator.hpp"
#include "render_pass.hpp"
#include "chain_allocator.hpp"
#include "sampler.hpp"
#include "shader.hpp"
#include "vulkan.hpp"
#include <memory>
#include <vector>

namespace Vulkan
{
enum class SwapchainRenderPass
{
	ColorOnly,
	Depth,
	DepthStencil
};


class Device
{
public:
	Device();
	~Device();
	void set_context(const VulkanContext &context);
	void init_swapchain(const std::vector<VkImage> swapchain_images, unsigned width, unsigned height, VkFormat format);

	void begin_frame(unsigned index);
	void flush_frame();
	void wait_idle();
	CommandBufferHandle request_command_buffer();
	void submit(CommandBufferHandle cmd);

	VkDevice get_device()
	{
		return device;
	}

	ShaderHandle create_shader(ShaderStage stage, const uint32_t *code, size_t size);
	ProgramHandle create_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
	                             size_t fragment_size);
	ProgramHandle create_program(const uint32_t *compute_data, size_t compute_size);
	void bake_program(Program &program);

	void *map_host_buffer(Buffer &buffer, MaliSDK::MemoryAccessFlags access);
	void unmap_host_buffer(const Buffer &buffer);

	BufferHandle create_buffer(const BufferCreateInfo &info, const void *initial);
	ImageHandle create_image(const ImageCreateInfo &info, const ImageInitialData *initial);
	ImageViewHandle create_image_view(const ImageViewCreateInfo &view_info);
	SamplerHandle create_sampler(const SamplerCreateInfo &info);
	const Sampler &get_stock_sampler(StockSampler sampler) const;

	void destroy_buffer(VkBuffer buffer);
	void destroy_image(VkImage image);
	void destroy_image_view(VkImageView view);
	void destroy_pipeline(VkPipeline pipeline);
	void destroy_sampler(VkSampler sampler);
	void destroy_framebuffer(VkFramebuffer framebuffer);
	void free_memory(const MaliSDK::DeviceAllocation &alloc);

	VkSemaphore set_acquire(VkSemaphore acquire);
	VkSemaphore set_release(VkSemaphore release);
	bool swapchain_touched() const;

	bool format_is_supported(VkFormat format, VkFormatFeatureFlags required) const;
	VkFormat get_default_depth_stencil_format() const;
	VkFormat get_default_depth_format() const;
	ImageView &get_transient_attachment(unsigned width, unsigned height, VkFormat format, unsigned index = 0);

	PipelineLayout *request_pipeline_layout(const CombinedResourceLayout &layout);
	DescriptorSetAllocator *request_descriptor_set_allocator(const DescriptorSetLayout &layout);
	const Framebuffer &request_framebuffer(const RenderPassInfo &info);
	const RenderPass &request_render_pass(const RenderPassInfo &info);

	uint64_t allocate_cookie()
	{
		return ++cookie;
	}

	RenderPassInfo get_swapchain_render_pass(SwapchainRenderPass style);
	ChainDataAllocation allocate_constant_data(VkDeviceSize size);
	ChainDataAllocation allocate_vertex_data(VkDeviceSize size);
	ChainDataAllocation allocate_index_data(VkDeviceSize size);

	const VkPhysicalDeviceMemoryProperties &get_memory_properties() const
	{
		return mem_props;
	}

	const VkPhysicalDeviceProperties &get_gpu_properties() const
	{
		return gpu_props;
	}

private:
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	MaliSDK::DeviceAllocator allocator;
	uint64_t cookie = 0;

	VkPhysicalDeviceMemoryProperties mem_props;
	VkPhysicalDeviceProperties gpu_props;
	void init_stock_samplers();

	struct PerFrame
	{
		PerFrame(Device *device, uint32_t queue_family_index);
		~PerFrame();
		void operator=(const PerFrame &) = delete;
		PerFrame(const PerFrame &) = delete;

		void cleanup();
		void begin();

		VkDevice device;
		CommandPool cmd_pool;
		ImageHandle backbuffer;
		FenceManager fence_manager;

		ChainAllocator vbo_chain, ibo_chain, ubo_chain;

		std::vector<MaliSDK::DeviceAllocation> allocations;
		std::vector<VkFramebuffer> destroyed_framebuffers;
		std::vector<VkSampler> destroyed_samplers;
		std::vector<VkPipeline> destroyed_pipelines;
		std::vector<VkImageView> destroyed_image_views;
		std::vector<VkImage> destroyed_images;
		std::vector<VkBuffer> destroyed_buffers;
		std::vector<CommandBufferHandle> submissions;
		bool swapchain_touched = false;
	};
	VkSemaphore wsi_acquire = VK_NULL_HANDLE;
	VkSemaphore wsi_release = VK_NULL_HANDLE;
	CommandBufferHandle staging_cmd;
	void begin_staging();
	void submit_queue();

	PerFrame &frame()
	{
		VK_ASSERT(current_swapchain_index < per_frame.size());
		VK_ASSERT(per_frame[current_swapchain_index]);
		return *per_frame[current_swapchain_index];
	}

	const PerFrame &frame() const
	{
		VK_ASSERT(current_swapchain_index < per_frame.size());
		VK_ASSERT(per_frame[current_swapchain_index]);
		return *per_frame[current_swapchain_index];
	}

	// The per frame structure must be destroyed after
	// the hashmap data structures below, so it must be declared before.
	std::vector<std::unique_ptr<PerFrame>> per_frame;

	unsigned current_swapchain_index = 0;
	uint32_t queue_family_index = 0;

	uint32_t find_memory_type(BufferDomain domain, uint32_t mask);
	uint32_t find_memory_type(ImageDomain domain, uint32_t mask);
	bool memory_type_is_device_optimal(uint32_t type) const;
	bool memory_type_is_host_visible(uint32_t type) const;

	SamplerHandle samplers[static_cast<unsigned>(StockSampler::Count)];
	HashMap<std::unique_ptr<PipelineLayout>> pipeline_layouts;
	HashMap<std::unique_ptr<DescriptorSetAllocator>> descriptor_set_allocators;
	FramebufferAllocator framebuffer_allocator;
	TransientAllocator transient_allocator;
	HashMap<std::unique_ptr<RenderPass>> render_passes;
	VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
};
}
