#pragma once

#include "vulkan.hpp"
#include "intrusive.hpp"
#include "buffer.hpp"
#include "image.hpp"

namespace Vulkan
{
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

         void copy_buffer(const Buffer &dst, VkDeviceSize dst_offset,
               const Buffer &src, VkDeviceSize src_offset, VkDeviceSize size);
         void copy_buffer(const Buffer &dst, const Buffer &src);

         void buffer_barrier(const Buffer &buffer,
               VkPipelineStageFlags src_stage,
               VkAccessFlags src_access,
               VkPipelineStageFlags dst_stage,
               VkAccessFlags dst_access);

      private:
         Device *device;
         VkCommandBuffer cmd;
         bool uses_swapchain = false;
   };

   using CommandBufferHandle = IntrusivePtr<CommandBuffer>;
}
