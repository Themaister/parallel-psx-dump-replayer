#pragma once

#include "vulkan.hpp"
#include "hashmap.hpp"
#include "image.hpp"
#include "command_pool.hpp"
#include "fence_manager.hpp"
#include "semaphore_manager.hpp"
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
      //void submit(CommandBufferHandle cmd);
      //CommandBufferHandle request_command_buffer();

      VkDevice get_device()
      {
         return device;
      }

      void destroy_buffer(VkBuffer buffer);
      void destroy_image(VkImage image);

   private:
      VkInstance instance = VK_NULL_HANDLE;
      VkPhysicalDevice gpu = VK_NULL_HANDLE;
      VkDevice device = VK_NULL_HANDLE;

      struct PerFrame
      {
         PerFrame(VkDevice device);
         ~PerFrame();
         void operator=(const PerFrame &) = delete;
         PerFrame(const PerFrame &) = delete;

         void begin();

         VkDevice device;
         CommandPool cmd_pool;
         ImageHandle backbuffer;
         FenceManager fence_manager;
         SemaphoreManager semaphore_manager;

         std::vector<VkImage> destroyed_images;
         std::vector<VkBuffer> destroyed_buffers;
         //std::vector<CommandBufferHandle> submissions;
      };

      PerFrame &frame()
      {
         return *per_frame[current_swapchain_index];
      }

      std::vector<std::unique_ptr<PerFrame>> per_frame;
      unsigned current_swapchain_index = 0;
      bool teardown_context = false;
};
}
