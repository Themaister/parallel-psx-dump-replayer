#include "image.hpp"
#include "device.hpp"

using namespace std;

namespace Vulkan
{

ImageView::ImageView(Device *device, VkImageView view, const ImageViewCreateInfo &info)
   : device(device), view(view), info(info)
{
}

ImageView::~ImageView()
{
   device->destroy_image_view(view);
}

Image::Image(Device *device, VkImage image, ImageViewHandle view, const MaliSDK::DeviceAllocation &alloc, const ImageCreateInfo &create_info)
   : device(device), image(image), view(move(view)), alloc(alloc), create_info(create_info)
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
