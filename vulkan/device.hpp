#pragma once

#include "vulkan.hpp"
#include "buffer.hpp"
#include "hashmap.hpp"
#include "image.hpp"
#include "command_pool.hpp"
#include "fence_manager.hpp"
#include "command_buffer.hpp"
#include "memory_allocator.hpp"
#include <memory>
#include <vector>

namespace Vulkan
{
class Device
{
   public:
      void set_context(const VulkanContext &context);
      void init_swapchain(const std::vector<VkImage> swapchain_images,
            unsigned width, unsigned height, VkFormat format);

      void begin_frame(unsigned index);
      void flush_frame();
      void wait_idle();
      CommandBufferHandle request_command_buffer();
      void submit(CommandBufferHandle cmd);

      VkDevice get_device()
      {
         return device;
      }

      BufferHandle create_buffer(const BufferCreateInfo &info, const void *initial);

      void destroy_buffer(VkBuffer buffer);
      void destroy_image(VkImage image);
      void destroy_image_view(VkImageView view);
      void free_memory(const MaliSDK::DeviceAllocation &alloc);

      VkSemaphore set_acquire(VkSemaphore acquire);
      VkSemaphore set_release(VkSemaphore release);
      bool swapchain_touched() const;

   private:
      VkInstance instance = VK_NULL_HANDLE;
      VkPhysicalDevice gpu = VK_NULL_HANDLE;
      VkDevice device = VK_NULL_HANDLE;
      VkQueue queue = VK_NULL_HANDLE;
      MaliSDK::DeviceAllocator allocator;

      VkPhysicalDeviceMemoryProperties mem_props;
      VkPhysicalDeviceProperties gpu_props;

      struct PerFrame
      {
         PerFrame(VkDevice device, uint32_t queue_family_index);
         ~PerFrame();
         void operator=(const PerFrame &) = delete;
         PerFrame(const PerFrame &) = delete;

         void begin();

         VkDevice device;
         CommandPool cmd_pool;
         ImageHandle backbuffer;
         FenceManager fence_manager;

         std::vector<MaliSDK::DeviceAllocation> allocations;
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
         return *per_frame[current_swapchain_index];
      }

      const PerFrame &frame() const
      {
         return *per_frame[current_swapchain_index];
      }

      std::vector<std::unique_ptr<PerFrame>> per_frame;
      unsigned current_swapchain_index = 0;
      uint32_t queue_family_index = 0;

      uint32_t find_memory_type(BufferDomain domain, uint32_t mask);
};
}
