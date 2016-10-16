#pragma once

#include "intrusive.hpp"
#include "memory_allocator.hpp"

namespace Vulkan
{
   class Device;

   enum class BufferDomain
   {
      Device,
      Host,
      CachedHost
   };

   struct BufferCreateInfo
   {
      BufferDomain domain;
      VkDeviceSize size;
      VkBufferUsageFlags usage;
   };

   class Buffer : public IntrusivePtrEnabled<Buffer>
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
   using BufferHandle = IntrusivePtr<Buffer>;
}
