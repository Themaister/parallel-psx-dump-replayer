#include "fence_manager.hpp"

namespace Vulkan
{
   FenceManager::FenceManager(VkDevice device)
      : device(device)
   {}

   VkFence FenceManager::request_cleared_fence()
   {
      if (index < fences.size())
      {
         return fences[index++];
      }
      else
      {
         VkFence fence;
         VkFenceCreateInfo info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
         vkCreateFence(device, &info, nullptr, &fence);
         fences.push_back(fence);
         return fence;
      }
   }

   void FenceManager::begin()
   {
      if (index)
         vkResetFences(device, index, fences.data());
      index = 0;
   }

   FenceManager::~FenceManager()
   {
      for (auto &fence : fences)
         vkDestroyFence(device, fence, nullptr);
   }
}
