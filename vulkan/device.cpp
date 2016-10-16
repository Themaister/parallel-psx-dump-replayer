#include "device.hpp"

using namespace std;

namespace Vulkan
{
void Device::set_context(const VulkanContext &context)
{
   instance = context.get_instance();
   gpu = context.get_gpu();
   device = context.get_device();
   queue_family_index = context.get_queue_family();

   mem_props = context.get_mem_props();
   gpu_props = context.get_gpu_props();

   allocator.init(gpu, device);
}

void Device::flush_frame()
{
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

CommandBufferHandle Device::request_command_buffer()
{
   auto cmd = frame().cmd_pool.request_command_buffer();
   return make_handle<CommandBuffer>(cmd);
}

bool Device::swapchain_touched() const
{
   return frame().swapchain_touched;
}

void Device::init_swapchain(const vector<VkImage> swapchain_images,
      unsigned width, unsigned height, VkFormat format)
{
   wait_idle();

   for (auto &frame : per_frame)
      frame->backbuffer.release();
   per_frame.clear();

   const auto info = ImageCreateInfo::render_target(width, height, format);

   for (auto &image : swapchain_images)
   {
      auto frame = unique_ptr<PerFrame>(new PerFrame(device, queue_family_index));
      frame->backbuffer = make_handle<Image>(this, image, MaliSDK::DeviceAllocation{}, info);
      per_frame.emplace_back(move(frame));
   }
}

Device::PerFrame::PerFrame(VkDevice device, uint32_t queue_family_index)
   : device(device), cmd_pool(device, queue_family_index), fence_manager(device)
{
}

void Device::free_memory(const MaliSDK::DeviceAllocation &alloc)
{
   frame().allocations.push_back(alloc);
}

void Device::destroy_image(VkImage image)
{
   frame().destroyed_images.push_back(image);
}

void Device::destroy_buffer(VkBuffer buffer)
{
   frame().destroyed_buffers.push_back(buffer);
}

void Device::wait_idle()
{
   for (auto &frame : per_frame)
      frame->begin();
}

void Device::begin_frame(unsigned index)
{
   current_swapchain_index = index;
   frame().begin();
}

void Device::PerFrame::begin()
{
   for (auto &image : destroyed_images)
      vkDestroyImage(device, image, nullptr);
   for (auto &buffer : destroyed_buffers)
      vkDestroyBuffer(device, buffer, nullptr);
   for (auto &alloc : allocations)
      alloc.freeImmediate();

   destroyed_images.clear();
   destroyed_buffers.clear();
   allocations.clear();

   swapchain_touched = false;
}

Device::PerFrame::~PerFrame()
{
   begin();
}

}
