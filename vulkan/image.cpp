#include "image.hpp"
#include "device.hpp"

namespace Vulkan
{
Image::Image(Device *device, VkImage image, const ImageCreateInfo &create_info)
   : device(device), image(image), create_info(create_info)
{
}

Image::~Image()
{
   //if (memory)
   //   device->destroy_image(image);
}
}
