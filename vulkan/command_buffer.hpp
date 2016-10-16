#pragma once

#include "vulkan.hpp"
#include "intrusive.hpp"

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

      private:
         Device *device;
         VkCommandBuffer cmd;
         bool uses_swapchain = false;
   };

   using CommandBufferHandle = IntrusivePtr<CommandBuffer>;
}
