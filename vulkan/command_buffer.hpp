#pragma once

#include "vulkan.hpp"
#include "intrusive.hpp"

namespace Vulkan
{
   class CommandBuffer : public IntrusivePtrEnabled
   {
      public:
         CommandBuffer(VkCommandBuffer cmd);
         VkCommandBuffer get_command_buffer()
         {
            return cmd;
         }

      private:
         VkCommandBuffer cmd;
   };

   using CommandBufferHandle = IntrusivePtr<CommandBuffer>;
}
