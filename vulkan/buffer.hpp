#pragma once

#include "intrusive.hpp"
#include "memory_allocator.hpp"

namespace Vulkan
{
   class Device;

   struct BufferCreateInfo
   {
      VkDeviceSize size;
      VkBufferUsageFlags usage;
   };

   class Buffer : public IntrusivePtrEnabled
   {
      public:
         Buffer(Device *device, VkBuffer buffer, const MaliSDK::DeviceAllocation &alloc,
               const BufferCreateInfo &info);
         ~Buffer();

      private:
         Device *device;
         VkBuffer buffer;
         MaliSDK::DeviceAllocation alloc;
         BufferCreateInfo info;
   };
}
