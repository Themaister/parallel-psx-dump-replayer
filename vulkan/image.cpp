#include "image.hpp"
#include "device.hpp"

namespace Vulkan
{
Image::Image(Device *device, VkImage image, const MaliSDK::DeviceAllocation &alloc, const ImageCreateInfo &create_info)
   : device(device), image(image), alloc(alloc), create_info(create_info)
{
}

Image::~Image()
{
   if (alloc.getMemory())
   {
      device->destroy_image(image);
      device->free_memory(alloc);
   }
}
}
