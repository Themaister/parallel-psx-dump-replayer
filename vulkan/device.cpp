#include "device.hpp"
#include "format.hpp"
#include <string.h>

using namespace std;

namespace Vulkan
{
Device::Device()
    : framebuffer_allocator(this)
{
}

ShaderHandle Device::create_shader(ShaderStage stage, const uint32_t *data, size_t size)
{
	return make_handle<Shader>(device, stage, data, size);
}

ProgramHandle Device::create_program(const uint32_t *compute_data, size_t compute_size)
{
	auto compute = make_handle<Shader>(device, ShaderStage::Compute, compute_data, compute_size);
	auto program = make_handle<Program>(this);
	program->set_shader(compute);
	bake_program(*program);
	return program;
}

ProgramHandle Device::create_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
                                     size_t fragment_size)
{
	auto vertex = make_handle<Shader>(device, ShaderStage::Vertex, vertex_data, vertex_size);
	auto fragment = make_handle<Shader>(device, ShaderStage::Fragment, fragment_data, fragment_size);
	auto program = make_handle<Program>(this);
	program->set_shader(vertex);
	program->set_shader(fragment);
	bake_program(*program);
	return program;
}

PipelineLayout *Device::request_pipeline_layout(const CombinedResourceLayout &layout)
{
	Hasher h;
	h.data(reinterpret_cast<const uint32_t *>(layout.sets), sizeof(layout.sets));
	h.data(reinterpret_cast<const uint32_t *>(layout.ranges), sizeof(layout.ranges));
	auto hash = h.get();
	auto itr = pipeline_layouts.find(hash);
	if (itr != end(pipeline_layouts))
		return itr->second.get();

	auto *pipe = new PipelineLayout(this, layout);
	pipeline_layouts.emplace(hash, pipe);
	return pipe;
}

DescriptorSetAllocator *Device::request_descriptor_set_allocator(const DescriptorSetLayout &layout)
{
	Hasher h;
	h.data(reinterpret_cast<const uint32_t *>(&layout), sizeof(layout));
	auto hash = h.get();
	auto itr = descriptor_set_allocators.find(hash);
	if (itr != end(descriptor_set_allocators))
		return itr->second.get();

	auto *allocator = new DescriptorSetAllocator(this, layout);
	descriptor_set_allocators.emplace(hash, allocator);
	return allocator;
}

void Device::bake_program(Program &program)
{
	CombinedResourceLayout layout;
	if (program.get_shader(ShaderStage::Vertex))
		layout.attribute_mask = program.get_shader(ShaderStage::Vertex)->get_layout().attribute_mask;

	for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
	{
		auto *shader = program.get_shader(static_cast<ShaderStage>(i));
		if (!shader)
			continue;

		auto &shader_layout = shader->get_layout();
		for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
		{
			layout.sets[set].sampled_image_mask |= shader_layout.sets[set].sampled_image_mask;
			layout.sets[set].storage_image_mask |= shader_layout.sets[set].storage_image_mask;
			layout.sets[set].uniform_buffer_mask |= shader_layout.sets[set].uniform_buffer_mask;
			layout.sets[set].storage_buffer_mask |= shader_layout.sets[set].storage_buffer_mask;
			layout.sets[set].stages |= shader_layout.sets[set].stages;
		}

		layout.ranges[i].stageFlags = 1u << i;
		layout.ranges[i].offset = shader_layout.push_constant_offset;
		layout.ranges[i].size = shader_layout.push_constant_range;
	}

	program.set_pipeline_layout(request_pipeline_layout(layout));
}

void Device::set_context(const VulkanContext &context)
{
	instance = context.get_instance();
	gpu = context.get_gpu();
	device = context.get_device();
	queue_family_index = context.get_queue_family();
	queue = context.get_queue();

	mem_props = context.get_mem_props();
	gpu_props = context.get_gpu_props();

	allocator.init(gpu, device);
	init_stock_samplers();
}

void Device::init_stock_samplers()
{
	SamplerCreateInfo info = {};
	info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	info.maxLod = VK_LOD_CLAMP_NONE;

	for (unsigned i = 0; i < static_cast<unsigned>(StockSampler::Count); i++)
	{
		auto mode = static_cast<StockSampler>(i);
		switch (mode)
		{
		default:
		case StockSampler::NearestClamp:
		case StockSampler::NearestWrap:
			info.magFilter = VK_FILTER_NEAREST;
			break;

		case StockSampler::LinearClamp:
		case StockSampler::LinearWrap:
			info.magFilter = VK_FILTER_LINEAR;
			break;
		}

		switch (mode)
		{
		default:
		case StockSampler::LinearWrap:
		case StockSampler::NearestWrap:
			info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;

		case StockSampler::LinearClamp:
		case StockSampler::NearestClamp:
			info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		}
		samplers[i] = create_sampler(info);
	}
}

void Device::submit(CommandBufferHandle cmd)
{
	if (staging_cmd)
	{
		vkEndCommandBuffer(staging_cmd->get_command_buffer());
		frame().submissions.push_back(staging_cmd);
		staging_cmd.reset();
	}

	vkEndCommandBuffer(cmd->get_command_buffer());
	frame().submissions.push_back(move(cmd));
}

void Device::submit_queue()
{
	vector<VkCommandBuffer> cmds;
	cmds.reserve(frame().submissions.size());

	vector<VkSubmitInfo> submits;
	submits.reserve(2);
	size_t last_cmd = 0;

	for (auto &cmd : frame().submissions)
	{
		if (cmd->swapchain_touched() && !frame().swapchain_touched)
		{
			if (!cmds.empty())
			{
				// Push all pending cmd buffers to their own submission.
				submits.emplace_back();

				auto &submit = submits.back();
				memset(&submit, 0, sizeof(submit));
				submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submit.pNext = nullptr;
				submit.commandBufferCount = cmds.size() - last_cmd;
				submit.pCommandBuffers = cmds.data() + last_cmd;
				last_cmd = cmds.size();
			}
			frame().swapchain_touched = true;
		}

		cmds.push_back(cmd->get_command_buffer());
	}

	if (cmds.size() > last_cmd)
	{
		// Push all pending cmd buffers to their own submission.
		submits.emplace_back();

		auto &submit = submits.back();
		memset(&submit, 0, sizeof(submit));
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.pNext = nullptr;
		submit.commandBufferCount = cmds.size() - last_cmd;
		submit.pCommandBuffers = cmds.data() + last_cmd;
		if (frame().swapchain_touched)
		{
			submit.waitSemaphoreCount = 1;
			submit.pWaitSemaphores = &wsi_acquire;
			static const uint32_t wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			submit.pWaitDstStageMask = &wait;
			submit.signalSemaphoreCount = 1;
			submit.pSignalSemaphores = &wsi_release;
		}
		last_cmd = cmds.size();
	}
	VkResult result =
	    vkQueueSubmit(queue, submits.size(), submits.data(), frame().fence_manager.request_cleared_fence());
	if (result != VK_SUCCESS)
		LOG("vkQueueSubmit failed.\n");
	frame().submissions.clear();
}

void Device::flush_frame()
{
	if (staging_cmd)
		frame().submissions.push_back(staging_cmd);
	staging_cmd.reset();

	submit_queue();
}

void Device::begin_staging()
{
	if (!staging_cmd)
		staging_cmd = request_command_buffer();
}

CommandBufferHandle Device::request_command_buffer()
{
	auto cmd = frame().cmd_pool.request_command_buffer();

	VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &info);
	return make_handle<CommandBuffer>(this, cmd);
}

VkSemaphore Device::set_acquire(VkSemaphore acquire)
{
	swap(acquire, wsi_acquire);
	return acquire;
}

VkSemaphore Device::set_release(VkSemaphore release)
{
	swap(release, wsi_release);
	return release;
}

const Sampler &Device::get_stock_sampler(StockSampler sampler) const
{
	return *samplers[static_cast<unsigned>(sampler)];
}

bool Device::swapchain_touched() const
{
	return frame().swapchain_touched;
}

Device::~Device()
{
	for (auto &frame : per_frame)
		frame->cleanup();
}

void Device::init_swapchain(const vector<VkImage> swapchain_images, unsigned width, unsigned height, VkFormat format)
{
	wait_idle();
	for (auto &frame : per_frame)
		frame->cleanup();
	per_frame.clear();

	const auto info = ImageCreateInfo::render_target(width, height, format);

	for (auto &image : swapchain_images)
	{
		auto frame = unique_ptr<PerFrame>(new PerFrame(device, queue_family_index));

		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view_info.image = image;
		view_info.format = format;
		view_info.components.r = VK_COMPONENT_SWIZZLE_R;
		view_info.components.g = VK_COMPONENT_SWIZZLE_G;
		view_info.components.b = VK_COMPONENT_SWIZZLE_B;
		view_info.components.a = VK_COMPONENT_SWIZZLE_A;
		view_info.subresourceRange.aspectMask = format_to_aspect_mask(format);
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.layerCount = 1;

		VkImageView image_view;
		if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
			LOG("Failed to create view for backbuffer.");

		frame->backbuffer = make_handle<Image>(this, image, image_view, MaliSDK::DeviceAllocation{}, info);
		per_frame.emplace_back(move(frame));
	}
}

Device::PerFrame::PerFrame(VkDevice device, uint32_t queue_family_index)
    : device(device)
    , cmd_pool(device, queue_family_index)
    , fence_manager(device)
{
}

void Device::free_memory(const MaliSDK::DeviceAllocation &alloc)
{
	frame().allocations.push_back(alloc);
}

void Device::destroy_pipeline(VkPipeline pipeline)
{
	frame().destroyed_pipelines.push_back(pipeline);
}

void Device::destroy_image_view(VkImageView view)
{
	frame().destroyed_image_views.push_back(view);
}

void Device::destroy_image(VkImage image)
{
	frame().destroyed_images.push_back(image);
}

void Device::destroy_buffer(VkBuffer buffer)
{
	frame().destroyed_buffers.push_back(buffer);
}

void Device::destroy_sampler(VkSampler sampler)
{
	frame().destroyed_samplers.push_back(sampler);
}

void Device::destroy_framebuffer(VkFramebuffer framebuffer)
{
	frame().destroyed_framebuffers.push_back(framebuffer);
}

void Device::wait_idle()
{
	if (!per_frame.empty())
		flush_frame();

	vkDeviceWaitIdle(device);
	for (auto &frame : per_frame)
		frame->begin();
}

void Device::begin_frame(unsigned index)
{
	current_swapchain_index = index;
	frame().begin();
	for (auto &allocator : descriptor_set_allocators)
		allocator.second->begin_frame();
}

void Device::PerFrame::begin()
{
	fence_manager.begin();
	cmd_pool.begin();

	for (auto &framebuffer : destroyed_framebuffers)
		vkDestroyFramebuffer(device, framebuffer, nullptr);
	for (auto &sampler : destroyed_samplers)
		vkDestroySampler(device, sampler, nullptr);
	for (auto &pipeline : destroyed_pipelines)
		vkDestroyPipeline(device, pipeline, nullptr);
	for (auto &view : destroyed_image_views)
		vkDestroyImageView(device, view, nullptr);
	for (auto &image : destroyed_images)
		vkDestroyImage(device, image, nullptr);
	for (auto &buffer : destroyed_buffers)
		vkDestroyBuffer(device, buffer, nullptr);
	for (auto &alloc : allocations)
		alloc.freeImmediate();

	destroyed_framebuffers.clear();
	destroyed_samplers.clear();
	destroyed_pipelines.clear();
	destroyed_image_views.clear();
	destroyed_images.clear();
	destroyed_buffers.clear();
	allocations.clear();

	swapchain_touched = false;
}

void Device::PerFrame::cleanup()
{
	backbuffer.reset();
}

Device::PerFrame::~PerFrame()
{
	cleanup();
	begin();
}

uint32_t Device::find_memory_type(BufferDomain domain, uint32_t mask)
{
	uint32_t desired, fallback;
	switch (domain)
	{
	case BufferDomain::Device:
		desired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		fallback = 0;
		break;

	case BufferDomain::Host:
		desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;

	case BufferDomain::CachedHost:
		desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & desired) == desired)
				return i;
		}
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & fallback) == fallback)
				return i;
		}
	}

	throw runtime_error("Couldn't find memory type.");
}

uint32_t Device::find_memory_type(ImageDomain domain, uint32_t mask)
{
	uint32_t desired, fallback;
	switch (domain)
	{
	case ImageDomain::Physical:
		desired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		fallback = 0;
		break;

	case ImageDomain::Transient:
		desired = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
		fallback = 0;
		break;
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & desired) == desired)
				return i;
		}
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & fallback) == fallback)
				return i;
		}
	}

	throw runtime_error("Couldn't find memory type.");
}

static inline VkImageViewType get_image_view_type(const ImageCreateInfo &create_info, const ImageViewCreateInfo *view)
{
	unsigned layers = view ? view->layers : create_info.layers;
	unsigned levels = view ? view->levels : create_info.levels;
	unsigned base_level = view ? view->base_level : 0;
	unsigned base_layer = view ? view->base_layer : 0;

	if (layers == VK_REMAINING_ARRAY_LAYERS)
		layers = create_info.layers - base_layer;
	if (levels == VK_REMAINING_MIP_LEVELS)
		levels = create_info.levels - base_level;

	switch (create_info.type)
	{
	case VK_IMAGE_TYPE_1D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height == 1);
		VK_ASSERT(create_info.depth == 1);
		VK_ASSERT(create_info.samples == VK_SAMPLE_COUNT_1_BIT);

		if (layers > 1)
			return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
		else
			return VK_IMAGE_VIEW_TYPE_1D;

	case VK_IMAGE_TYPE_2D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height >= 1);
		VK_ASSERT(create_info.depth == 1);

		if ((create_info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && (layers % 6) == 0)
		{
			VK_ASSERT(create_info.width == create_info.height);

			if (layers > 6)
				return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			else
				return VK_IMAGE_VIEW_TYPE_CUBE;
		}
		else
		{
			if (layers > 6)
				return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			else
				return VK_IMAGE_VIEW_TYPE_2D;
		}

	case VK_IMAGE_TYPE_3D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height >= 1);
		VK_ASSERT(create_info.depth >= 1);
		return VK_IMAGE_VIEW_TYPE_3D;

	default:
		VK_ASSERT(0 && "bogus");
		return VK_IMAGE_VIEW_TYPE_RANGE_SIZE;
	}
}

ImageViewHandle Device::create_image_view(const ImageViewCreateInfo &create_info)
{
	auto &image_create_info = create_info.image->get_create_info();

	VkFormat format = create_info.format != VK_FORMAT_UNDEFINED ? create_info.format : image_create_info.format;

	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	view_info.image = create_info.image->get_image();
	view_info.format = format;
	view_info.components = create_info.swizzle;
	view_info.subresourceRange.aspectMask = format_to_aspect_mask(format);
	view_info.subresourceRange.baseMipLevel = create_info.base_level;
	view_info.subresourceRange.baseArrayLayer = create_info.base_layer;
	view_info.subresourceRange.levelCount = create_info.levels;
	view_info.subresourceRange.layerCount = create_info.layers;
	view_info.viewType = get_image_view_type(image_create_info, &create_info);

	VkImageView image_view;
	if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
		return nullptr;

	ImageViewCreateInfo tmp = create_info;
	tmp.format = format;
	return make_handle<ImageView>(this, image_view, tmp);
}

ImageHandle Device::create_image(const ImageCreateInfo &create_info, const ImageInitialData *initial)
{
	VkImage image;
	VkMemoryRequirements reqs;
	MaliSDK::DeviceAllocation allocation;

	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	info.format = create_info.format;
	info.extent.width = create_info.width;
	info.extent.height = create_info.height;
	info.extent.depth = create_info.depth;
	info.imageType = create_info.type;
	info.mipLevels = create_info.levels;
	info.arrayLayers = create_info.layers;
	info.samples = create_info.samples;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = create_info.usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (create_info.domain == ImageDomain::Transient)
		info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

	if (create_info.usage & VK_IMAGE_USAGE_STORAGE_BIT)
		info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

	if (info.mipLevels == 0)
		info.mipLevels = image_num_miplevels(info.extent);

	VK_ASSERT(format_is_supported(create_info.format, image_usage_to_features(info.usage)));

	if (vkCreateImage(device, &info, nullptr, &image) != VK_SUCCESS)
		return nullptr;

	vkGetImageMemoryRequirements(device, image, &reqs);

	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
	if (!allocator.allocate(reqs.size, reqs.alignment, memory_type, MaliSDK::ALLOCATION_TILING_OPTIMAL, &allocation))
	{
		vkDestroyImage(device, image, nullptr);
		return nullptr;
	}

	if (vkBindImageMemory(device, image, allocation.getDeviceMemory(), allocation.getOffset()) != VK_SUCCESS)
	{
		allocation.freeImmediate();
		vkDestroyImage(device, image, nullptr);
		return nullptr;
	}

	auto tmpinfo = create_info;
	tmpinfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	tmpinfo.levels = info.mipLevels;
	if (tmpinfo.domain == ImageDomain::Transient)
		tmpinfo.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

	// Create a default image view.
	VkImageView image_view = VK_NULL_HANDLE;
	if (info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
	{
		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view_info.image = image;
		view_info.format = create_info.format;
		view_info.components.r = VK_COMPONENT_SWIZZLE_R;
		view_info.components.g = VK_COMPONENT_SWIZZLE_G;
		view_info.components.b = VK_COMPONENT_SWIZZLE_B;
		view_info.components.a = VK_COMPONENT_SWIZZLE_A;
		view_info.subresourceRange.aspectMask = format_to_aspect_mask(view_info.format);
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		view_info.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
		view_info.viewType = get_image_view_type(create_info, nullptr);

		if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
		{
			allocation.freeImmediate();
			vkDestroyImage(device, image, nullptr);
			return nullptr;
		}
	}

	auto handle = make_handle<Image>(this, image, image_view, allocation, tmpinfo);
	begin_staging();

	// Copy initial data to texture.
	if (initial)
	{
		bool generate_mips = (create_info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;
		unsigned copy_levels = generate_mips ? 1u : info.mipLevels;

		staging_cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                           VK_ACCESS_TRANSFER_WRITE_BIT);
		handle->set_layout(VK_IMAGE_LAYOUT_GENERAL);

		VkExtent3D extent = { create_info.width, create_info.height, create_info.depth };

		VkImageSubresourceLayers subresource = {
			format_to_aspect_mask(info.format), 0, 0, create_info.layers,
		};

		for (unsigned i = 0; i < copy_levels; i++)
		{
			uint32_t row_length = initial[i].row_length ? initial[i].row_length : create_info.width;
			uint32_t array_height = initial[i].array_height ? initial[i].array_height : create_info.height;

			VkDeviceSize size = format_pixel_size(create_info.format) * create_info.layers * create_info.width *
			                    (initial[i].array_height ? initial[i].array_height : create_info.height);

			auto temp = create_buffer({ BufferDomain::Host, size, 0 }, initial[i].data);

			subresource.mipLevel = i;

			staging_cmd->copy_buffer_to_image(*handle, *temp, 0, { 0, 0, 0 }, extent, row_length, array_height,
			                                  subresource);

			extent.width = max(extent.width >> 1u, 1u);
			extent.height = max(extent.height >> 1u, 1u);
			extent.depth = max(extent.depth >> 1u, 1u);
		}

		if (generate_mips)
		{
			VkOffset3D size = { int(create_info.width), int(create_info.height), int(create_info.depth) };
			const VkOffset3D origin = { 0, 0, 0 };

			for (unsigned i = 1; i < tmpinfo.levels; i++)
			{
				staging_cmd->image_barrier(*handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

				VkOffset3D src_size = size;
				size.x = max(size.x >> 1, 1);
				size.y = max(size.y >> 1, 1);
				size.z = max(size.z >> 1, 1);

				staging_cmd->blit_image(*handle, *handle, origin, size, origin, src_size, i, i - 1, 0, 0,
				                        create_info.layers);
			}
		}

		staging_cmd->image_barrier(*handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                           image_usage_to_possible_stages(info.usage),
		                           image_usage_to_possible_access(info.usage));
	}
	else
	{
		staging_cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, image_usage_to_possible_stages(info.usage),
		                           image_usage_to_possible_access(info.usage));
		handle->set_layout(VK_IMAGE_LAYOUT_GENERAL);
	}

	return handle;
}

SamplerHandle Device::create_sampler(const SamplerCreateInfo &sampler_info)
{
	VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	info.magFilter = sampler_info.magFilter;
	info.minFilter = sampler_info.minFilter;
	info.mipmapMode = sampler_info.mipmapMode;
	info.addressModeU = sampler_info.addressModeU;
	info.addressModeV = sampler_info.addressModeV;
	info.addressModeW = sampler_info.addressModeW;
	info.mipLodBias = sampler_info.mipLodBias;
	info.anisotropyEnable = sampler_info.anisotropyEnable;
	info.maxAnisotropy = sampler_info.maxAnisotropy;
	info.compareEnable = sampler_info.compareEnable;
	info.compareOp = sampler_info.compareOp;
	info.minLod = sampler_info.minLod;
	info.maxLod = sampler_info.maxLod;
	info.borderColor = sampler_info.borderColor;
	info.unnormalizedCoordinates = sampler_info.unnormalizedCoordinates;

	VkSampler sampler;
	if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
		return nullptr;
	return make_handle<Sampler>(this, sampler, sampler_info);
}

BufferHandle Device::create_buffer(const BufferCreateInfo &create_info, const void *initial)
{
	VkBuffer buffer;
	VkMemoryRequirements reqs;
	MaliSDK::DeviceAllocation allocation;

	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	info.size = create_info.size;
	info.usage = create_info.usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	if (vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS)
		return nullptr;

	vkGetBufferMemoryRequirements(device, buffer, &reqs);

	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
	if (!allocator.allocate(reqs.size, reqs.alignment, memory_type, MaliSDK::ALLOCATION_TILING_LINEAR, &allocation))
	{
		vkDestroyBuffer(device, buffer, nullptr);
		return nullptr;
	}

	if (vkBindBufferMemory(device, buffer, allocation.getDeviceMemory(), allocation.getOffset()) != VK_SUCCESS)
	{
		allocation.freeImmediate();
		vkDestroyBuffer(device, buffer, nullptr);
		return nullptr;
	}

	auto tmpinfo = create_info;
	tmpinfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	auto handle = make_handle<Buffer>(this, buffer, allocation, tmpinfo);

	if (create_info.domain == BufferDomain::Device && initial && !memory_type_is_host_visible(memory_type))
	{
		begin_staging();
		const BufferCreateInfo staging_info = { BufferDomain::Host, create_info.size, 0 };
		auto tmp = create_buffer(staging_info, initial);
		staging_cmd->copy_buffer(*handle, *tmp);
		staging_cmd->buffer_barrier(*handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                            buffer_usage_to_possible_stages(info.usage),
		                            buffer_usage_to_possible_access(info.usage));
	}
	else if (initial)
	{
		void *ptr = allocator.mapMemory(&allocation, MaliSDK::MEMORY_ACCESS_WRITE);
		if (!ptr)
			return nullptr;
		memcpy(ptr, initial, create_info.size);
		allocator.unmapMemory(allocation);
	}
	return handle;
}

bool Device::memory_type_is_device_optimal(uint32_t type) const
{
	return (mem_props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
}

bool Device::memory_type_is_host_visible(uint32_t type) const
{
	return (mem_props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

bool Device::format_is_supported(VkFormat format, VkFormatFeatureFlags required) const
{
	VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
	auto flags = props.optimalTilingFeatures;
	return (flags & required) == required;
}

VkFormat Device::get_default_depth_stencil_format() const
{
	if (format_is_supported(VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D24_UNORM_S8_UINT;
	if (format_is_supported(VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D32_SFLOAT_S8_UINT;

	return VK_FORMAT_UNDEFINED;
}

VkFormat Device::get_default_depth_format() const
{
	if (format_is_supported(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D32_SFLOAT;
	if (format_is_supported(VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_X8_D24_UNORM_PACK32;
	if (format_is_supported(VK_FORMAT_D16_UNORM, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D16_UNORM;

	return VK_FORMAT_UNDEFINED;
}

const RenderPass &Device::request_render_pass(const RenderPassInfo &info)
{
	Hasher h;
	VkFormat formats[VULKAN_NUM_ATTACHMENTS];
	VkFormat depth_stencil;
	uint32_t lazy = 0;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		formats[i] = info.color_attachments[i] ? info.color_attachments[i]->get_format() : VK_FORMAT_UNDEFINED;
		if (info.color_attachments[i]->get_image().get_create_info().domain == ImageDomain::Transient)
			lazy |= 1u << i;
	}

	if (info.depth_stencil && info.depth_stencil->get_image().get_create_info().domain == ImageDomain::Transient)
		lazy |= 1u << info.num_color_attachments;

	depth_stencil = info.depth_stencil ? info.depth_stencil->get_format() : VK_FORMAT_UNDEFINED;
	h.data(formats, info.num_color_attachments * sizeof(VkFormat));
	h.u32(info.num_color_attachments);
	h.u32(depth_stencil);
	h.u32(info.op_flags);
	h.u32(lazy);

	auto hash = h.get();
	auto itr = render_passes.find(hash);
	if (itr != end(render_passes))
		return *itr->second.get();
	else
	{
		RenderPass *pass = new RenderPass(this, info);
		render_passes.emplace(hash, pass);
		return *pass;
	}
}

const Framebuffer &Device::request_framebuffer(const RenderPassInfo &info)
{
	return framebuffer_allocator.request_framebuffer(info);
}

RenderPassInfo Device::get_swapchain_render_pass()
{
	RenderPassInfo info;
	info.num_color_attachments = 1;
	info.color_attachments[0] = &frame().backbuffer->get_view();
	info.op_flags = RENDER_PASS_OP_COLOR_OPTIMAL_BIT | RENDER_PASS_OP_CLEAR_ALL_BIT | RENDER_PASS_OP_STORE_COLOR_BIT;
	return info;
}
}
