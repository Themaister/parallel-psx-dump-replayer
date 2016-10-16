#pragma once

#include "intrusive.hpp"
#include "vulkan.hpp"
#include "memory_allocator.hpp"

namespace Vulkan
{
   class Device;
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

   class Image;
   struct ImageViewCreateInfo
   {
      Image *image;
      VkFormat format;
      unsigned base_level;
      unsigned levels;
      unsigned base_layer;
      unsigned layers;
   };

   class ImageView : public IntrusivePtrEnabled<ImageView>
   {
      public:
         ImageView(Device *device, VkImageView view, const ImageViewCreateInfo &info);
         ~ImageView();

      private:
         Device *device;
         VkImageView view;
         ImageViewCreateInfo info;
   };
   using ImageViewHandle = IntrusivePtr<ImageView>;

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

   class Image : public IntrusivePtrEnabled<Image>
   {
      public:
         Image(Device *device, VkImage image, ImageViewHandle view,
               const MaliSDK::DeviceAllocation &alloc, const ImageCreateInfo &info);
         ~Image();
         Image(Image &&) = delete;
         Image &operator=(Image &&) = delete;

      private:
         Device *device;
         VkImage image;
         ImageViewHandle view;
         MaliSDK::DeviceAllocation alloc;
         ImageCreateInfo create_info;
   };

   using ImageHandle = IntrusivePtr<Image>;
}
