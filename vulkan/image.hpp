#pragma once

#include "format.hpp"
#include "intrusive.hpp"
#include "memory_allocator.hpp"
#include "vulkan.hpp"

namespace Vulkan
{
class Device;

static inline VkPipelineStageFlags image_usage_to_possible_stages(VkImageUsageFlags usage)
{
   return 0;
}

static inline VkAccessFlags image_usage_to_possible_access(VkImageUsageFlags usage)
{
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

enum ImageViewCreateFlagBits
{
	IMAGE_VIEW_GENERATE_MIPS_BIT = 1 << 0
};
using ImageViewCreateFlags = uint32_t;

class Image;
struct ImageViewCreateInfo
{
	Image *image;
	VkFormat format;
	unsigned base_level;
	unsigned levels;
	unsigned base_layer;
	unsigned layers;
	ImageViewCreateFlags flags;
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

	static ImageCreateInfo render_target(unsigned width, unsigned height, VkFormat format)
	{
		return { ImageDomain::Physical, width, height, 1, 1, format, VK_IMAGE_TYPE_2D, 1,
			     VkImageUsageFlags((format_is_depth_stencil(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
			                                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
			                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
			     VK_SAMPLE_COUNT_1_BIT, 0 };
	}
};

class Image : public IntrusivePtrEnabled<Image>
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
