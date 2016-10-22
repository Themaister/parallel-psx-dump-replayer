#pragma once

#include "cookie.hpp"
#include "format.hpp"
#include "intrusive.hpp"
#include "memory_allocator.hpp"
#include "vulkan.hpp"

namespace Vulkan
{
class Device;

static inline VkPipelineStageFlags image_usage_to_possible_stages(VkImageUsageFlags usage)
{
	VkPipelineStageFlags flags = 0;

	if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
		         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
		flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

	return flags;
}

static inline VkAccessFlags image_usage_to_possible_access(VkImageUsageFlags usage)
{
	VkAccessFlags flags = 0;

	if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		flags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		flags |= VK_ACCESS_SHADER_READ_BIT;
	if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		flags |= VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		flags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
		flags |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

	return 0;
}

static inline uint32_t image_num_miplevels(const VkExtent3D &extent)
{
	uint32_t size = std::max(std::max(extent.width, extent.height), extent.depth);
	uint32_t levels = 0;
	while (size)
	{
		levels++;
		size >>= 1;
	}
	return levels;
}

static inline VkFormatFeatureFlags image_usage_to_features(VkImageUsageFlags usage)
{
	VkFormatFeatureFlags flags = 0;
	if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		flags |= VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

	return flags;
}

struct ImageInitialData
{
	const void *data;
	unsigned row_length;
	unsigned array_height;
};

enum ImageMiscFlagBits
{
	IMAGE_MISC_GENERATE_MIPS_BIT = 1 << 0
};
using ImageMiscFlags = uint32_t;

class Image;
struct ImageViewCreateInfo
{
	Image *image = nullptr;
	VkFormat format = VK_FORMAT_UNDEFINED;
	unsigned base_level = 0;
	unsigned levels = VK_REMAINING_MIP_LEVELS;
	unsigned base_layer = 0;
	unsigned layers = VK_REMAINING_ARRAY_LAYERS;
   VkComponentMapping swizzle = {
      VK_COMPONENT_SWIZZLE_R,
      VK_COMPONENT_SWIZZLE_G,
      VK_COMPONENT_SWIZZLE_B,
      VK_COMPONENT_SWIZZLE_A,
   };
};

class ImageView : public IntrusivePtrEnabled<ImageView>, public Cookie
{
public:
	ImageView(Device *device, VkImageView view, const ImageViewCreateInfo &info);
	~ImageView();

   VkImageView get_view() const
   {
      return view;
   }

   VkFormat get_format() const
   {
      return info.format;
   }

   const Image &get_image() const
   {
      return *info.image;
   }

private:
	Device *device;
	VkImageView view;
	ImageViewCreateInfo info;
};
using ImageViewHandle = IntrusivePtr<ImageView>;

enum class ImageDomain
{
	Physical,
	Transient
};

struct ImageCreateInfo
{
	ImageDomain domain = ImageDomain::Physical;
	unsigned width = 0;
	unsigned height = 0;
	unsigned depth = 1;
	unsigned levels = 1;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkImageType type = VK_IMAGE_TYPE_2D;
	unsigned layers = 1;
	VkImageUsageFlags usage = 0;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkImageCreateFlags flags = 0;
	ImageMiscFlags misc = 0;

	static ImageCreateInfo immutable_2d_image(unsigned width, unsigned height, VkFormat format, bool mipmapped = false)
	{
		return { ImageDomain::Physical,
			     width,
			     height,
			     1,
			     mipmapped ? 0u : 1u,
			     format,
			     VK_IMAGE_TYPE_2D,
			     1,
			     VK_IMAGE_USAGE_SAMPLED_BIT,
			     VK_SAMPLE_COUNT_1_BIT,
			     0,
			     mipmapped ? unsigned(IMAGE_MISC_GENERATE_MIPS_BIT) : 0u };
	}

	static ImageCreateInfo render_target(unsigned width, unsigned height, VkFormat format)
	{
		return { ImageDomain::Physical, width, height, 1, 1, format, VK_IMAGE_TYPE_2D, 1,
			     VkImageUsageFlags((format_is_depth_stencil(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
			                                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
			                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
			     VK_SAMPLE_COUNT_1_BIT, 0, 0 };
	}
};

class Image : public IntrusivePtrEnabled<Image>, public Cookie
{
public:
	Image(Device *device, VkImage image, VkImageView default_view, const MaliSDK::DeviceAllocation &alloc,
	      const ImageCreateInfo &info);
	~Image();
	Image(Image &&) = delete;
	Image &operator=(Image &&) = delete;

	const ImageView &get_view() const
	{
		VK_ASSERT(view);
		return *view;
	}

	VkImage get_image() const
	{
		return image;
	}

   VkFormat get_format() const
   {
      return create_info.format;
   }

	const ImageCreateInfo &get_create_info() const
	{
		return create_info;
	}

	VkImageLayout get_layout() const
	{
		return layout;
	}

	void set_layout(VkImageLayout new_layout)
	{
		layout = new_layout;
	}

private:
	Device *device;
	VkImage image;
	ImageViewHandle view;
	MaliSDK::DeviceAllocation alloc;
	ImageCreateInfo create_info;

	VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL;
};

using ImageHandle = IntrusivePtr<Image>;
}
