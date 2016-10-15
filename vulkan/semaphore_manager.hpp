#pragma once

#include <vector>
#include "vulkan.hpp"

namespace Vulkan
{
   class SemaphoreManager
   {
      public:
         SemaphoreManager(VkDevice device);
         ~SemaphoreManager();

         VkSemaphore request_cleared_semaphore();
         void recycle(VkSemaphore semaphore);

      private:
         VkDevice device;
         std::vector<VkSemaphore> semaphores;
   };
}
