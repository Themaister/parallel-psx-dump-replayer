#include "device.hpp"
#include <string.h>

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
      frame->backbuffer.reset();
   per_frame.clear();

   const auto info = ImageCreateInfo::render_target(width, height, format);

   for (auto &image : swapchain_images)
   {
      auto frame = unique_ptr<PerFrame>(new PerFrame(device, queue_family_index));
      frame->backbuffer = make_handle<Image>(this, image, nullptr, MaliSDK::DeviceAllocation{}, info);
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
   fence_manager.begin();
   cmd_pool.begin();

   for (auto &view : destroyed_image_views)
      vkDestroyImageView(device, view, nullptr);
   for (auto &image : destroyed_images)
      vkDestroyImage(device, image, nullptr);
   for (auto &buffer : destroyed_buffers)
      vkDestroyBuffer(device, buffer, nullptr);
   for (auto &alloc : allocations)
      alloc.freeImmediate();

   destroyed_image_views.clear();
   destroyed_images.clear();
   destroyed_buffers.clear();
   allocations.clear();

   swapchain_touched = false;
}

Device::PerFrame::~PerFrame()
{
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

BufferHandle Device::create_buffer(const BufferCreateInfo &create_info, const void *initial)
{
   VkBuffer buffer;
   VkMemoryRequirements reqs;
   MaliSDK::DeviceAllocation allocation;

   VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
   info.size = create_info.size;
   info.usage = create_info.usage;

   if (vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS)
      return nullptr;

   vkGetBufferMemoryRequirements(device, buffer, &reqs);

   uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
   if (!allocator.allocate(reqs.size, memory_type, MaliSDK::ALLOCATION_TILING_LINEAR, &allocation))
   {
      vkDestroyBuffer(device, buffer, nullptr);
      return nullptr;
   }

   if (vkBindBufferMemory(device, buffer,
            allocation.getDeviceMemory(), allocation.getOffset()) != VK_SUCCESS)
   {
      allocation.freeImmediate();
      vkDestroyBuffer(device, buffer, nullptr);
      return nullptr;
   }

   if (initial)
   {
      void *ptr = allocator.mapMemory(&allocation, MaliSDK::MEMORY_ACCESS_WRITE);
      if (!ptr)
      {
         allocation.freeImmediate();
         vkDestroyBuffer(device, buffer, nullptr);
         return nullptr;
      }
      memcpy(ptr, initial, create_info.size);
      allocator.unmapMemory(allocation);
   }

   return make_handle<Buffer>(this, buffer, allocation, create_info);
}

}
