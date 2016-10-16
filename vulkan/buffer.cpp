#include "buffer.hpp"
#include "device.hpp"

namespace Vulkan
{
   Buffer::Buffer(Device *device, VkBuffer buffer, const MaliSDK::DeviceAllocation &alloc,
         const BufferCreateInfo &info)
      : device(device), buffer(buffer), alloc(alloc), info(info)
   {
   }

   Buffer::~Buffer()
   {
      device->destroy_buffer(buffer);
      device->free_memory(alloc);
   }
}
