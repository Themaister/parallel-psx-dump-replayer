#include "image.hpp"
#include "device.hpp"

using namespace std;

namespace Vulkan
{

ImageView::ImageView(Device *device, VkImageView view, const ImageViewCreateInfo &info)
    : device(device)
    , view(view)
    , info(info)
{
}

ImageView::~ImageView()
{
	device->destroy_image_view(view);
}

Image::Image(Device *device, VkImage image, VkImageView default_view, const MaliSDK::DeviceAllocation &alloc,
             const ImageCreateInfo &create_info)
    : device(device)
    , image(image)
    , view(move(view))
    , alloc(alloc)
    , create_info(create_info)
{
	if (default_view != VK_NULL_HANDLE)
	{
		view = make_handle<ImageView>(device, default_view,
		                              ImageViewCreateInfo{
		                                  this, create_info.format, 0, create_info.levels, 0, create_info.layers,
		                              });
	}
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
