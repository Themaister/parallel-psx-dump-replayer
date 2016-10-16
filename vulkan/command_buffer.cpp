#include "command_buffer.hpp"

namespace Vulkan
{
   CommandBuffer::CommandBuffer(Device *device, VkCommandBuffer cmd)
      : device(device), cmd(cmd)
   {
   }
}
