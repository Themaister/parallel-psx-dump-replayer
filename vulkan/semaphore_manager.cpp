#include "semaphore_manager.hpp"

namespace Vulkan
{
   SemaphoreManager::SemaphoreManager(VkDevice device)
      : device(device)
   {
   }

   SemaphoreManager::~SemaphoreManager()
   {
      for (auto &sem : semaphores)
         vkDestroySemaphore(device, sem, nullptr);
   }

   void SemaphoreManager::recycle(VkSemaphore sem)
   {
      semaphores.push_back(sem);
   }

   VkSemaphore SemaphoreManager::request_cleared_semaphore()
   {
      if (semaphores.empty())
      {
         VkSemaphore semaphore;
         VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
         vkCreateSemaphore(device, &info, nullptr, &semaphore);
         return semaphore;
      }
      else
      {
         auto sem = semaphores.back();
         semaphores.pop_back();
         return sem;
      }
   }
}
