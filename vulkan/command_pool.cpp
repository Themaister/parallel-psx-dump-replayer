#include "command_pool.hpp"

namespace Vulkan
{
CommandPool::CommandPool(VkDevice device)
   : device(device)
{
}

CommandPool::~CommandPool()
{
   if (!buffers.empty())
      vkFreeCommandBuffers(device, pool, buffers.size(), buffers.data());
   vkDestroyCommandPool(device, pool, nullptr);
}

VkCommandBuffer CommandPool::request_command_buffer()
{
   if (index < buffers.size())
   {
      return buffers[index++];
   }
   else
   {
      VkCommandBuffer cmd;
      VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      info.commandPool = pool;
      info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      info.commandBufferCount = 1;

      vkAllocateCommandBuffers(device, &info, &cmd);
      buffers.push_back(cmd);
      index++;
      return cmd;
   }
}

void CommandPool::begin()
{
   vkResetCommandPool(device, pool, 0);
   index = 0;
}
}
