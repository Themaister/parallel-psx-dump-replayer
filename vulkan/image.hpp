#pragma once

#include "intrusive.hpp"
#include "vulkan.hpp"
#include "memory_allocator.hpp"

namespace Vulkan
{
   static bool format_is_depth_stencil(VkFormat format)
   {
      switch (format)
      {
         case VK_FORMAT_D16_UNORM:
         case VK_FORMAT_D16_UNORM_S8_UINT:
         case VK_FORMAT_D24_UNORM_S8_UINT:
         case VK_FORMAT_D32_SFLOAT:
         case VK_FORMAT_X8_D24_UNORM_PACK32:
         case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;

         default:
            return false;
      }
   }

   class Device;
   struct ImageCreateInfo
   {
      unsigned width;
      unsigned height;
      unsigned levels;
      VkFormat format;
      VkImageType type;
      unsigned layers;
      VkImageUsageFlags usage;

      static ImageCreateInfo render_target(unsigned width, unsigned height, VkFormat format)
      {
         return { width, height, 1, format, VK_IMAGE_TYPE_2D, 1,
            VkImageUsageFlags((format_is_depth_stencil(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT) };
      }
   };

   class Image : public IntrusivePtrEnabled
   {
      public:
         Image(Device *device, VkImage image, const MaliSDK::DeviceAllocation &alloc, const ImageCreateInfo &info);
         ~Image();
         Image(Image &&) = delete;
         Image &operator=(Image &&) = delete;

      private:
         Device *device;
         VkImage image;
         VkImageView view;
         MaliSDK::DeviceAllocation alloc;
         ImageCreateInfo create_info;
   };

   using ImageHandle = IntrusivePtr<Image>;
}
