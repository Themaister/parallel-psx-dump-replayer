#include "vulkan_symbol_wrapper.h"
#include <GLFW/glfw3.h>
#include "wsi.hpp"

using namespace std;

namespace Vulkan
{

bool WSI::alive()
{
   glfwPollEvents();
   return glfwWindowShouldClose(window);
}

static void fb_size_cb(GLFWwindow *window, int width, int height)
{
   auto *wsi = static_cast<WSI *>(glfwGetWindowUserPointer(window));
   wsi->update_framebuffer(width, height);
}

bool WSI::init(unsigned width, unsigned height)
{
   if (!glfwInit())
      return false;

   if (!VulkanContext::init_loader(glfwGetInstanceProcAddress))
      return false;

   uint32_t count;
   const char **ext = glfwGetRequiredInstanceExtensions(&count);
   const char *device_ext = "VK_KHR_swapchain";
   context = unique_ptr<VulkanContext>(new VulkanContext(ext, count, &device_ext, 1));

   VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkDestroySurfaceKHR);
   VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkGetPhysicalDeviceSurfaceSupportKHR);
   VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
   VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkGetPhysicalDeviceSurfaceFormatsKHR);
   VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkGetPhysicalDeviceSurfacePresentModesKHR);

   VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkCreateSwapchainKHR);
   VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkDestroySwapchainKHR);
   VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkGetSwapchainImagesKHR);
   VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkAcquireNextImageKHR);
   VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkQueuePresentKHR);

   glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
   window = glfwCreateWindow(width, height, "GLFW Window", nullptr, nullptr);
   if (glfwCreateWindowSurface(context->get_instance(), window, nullptr, &surface) != VK_SUCCESS)
      return false;

   VkBool32 supported = false;
   vkGetPhysicalDeviceSurfaceSupportKHR(context->get_gpu(), context->get_queue_family(), surface, &supported);
   if (!supported)
      return false;

   if (!init_swapchain(width, height))
      return false;

   glfwSetWindowUserPointer(window, this);
   glfwSetFramebufferSizeCallback(window, fb_size_cb);

   return true;
}

void WSI::update_framebuffer(unsigned width, unsigned height)
{
   vkDeviceWaitIdle(context->get_device());
   init_swapchain(width, height);
}

bool WSI::init_swapchain(unsigned width, unsigned height)
{
   VkSurfaceCapabilitiesKHR surface_properties;
   auto gpu = context->get_gpu();
   V(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surface_properties));

   uint32_t format_count;
   vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, nullptr);
   vector<VkSurfaceFormatKHR> formats(format_count);
   vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, formats.data());

   VkSurfaceFormatKHR format;
   if (format_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
   {
      format = formats[0];
      format.format = VK_FORMAT_B8G8R8A8_UNORM;
   }
   else
   {
      if (format_count == 0)
      {
         LOG("Surface has no formats.\n");
         return false;
      }

      format = formats[0];
   }

   VkExtent2D swapchain_size;
   if (surface_properties.currentExtent.width == -1u)
   {
      swapchain_size.width = width;
      swapchain_size.height = height;
   }
   else
      swapchain_size = surface_properties.currentExtent;

   VkPresentModeKHR swapchain_present_mode = VK_PRESENT_MODE_FIFO_KHR;

   uint32_t desired_swapchain_images = surface_properties.minImageCount + 1;
   if ((surface_properties.maxImageCount > 0) && (desired_swapchain_images > surface_properties.maxImageCount))
      desired_swapchain_images = surface_properties.maxImageCount;

   VkSurfaceTransformFlagBitsKHR pre_transform;
   if (surface_properties.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
      pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   else
      pre_transform = surface_properties.currentTransform;

   VkSwapchainKHR old_swapchain = swapchain;

   VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
   info.surface = surface;
   info.minImageCount = desired_swapchain_images;
   info.imageFormat = format.format;
   info.imageColorSpace = format.colorSpace;
   info.imageExtent.width = swapchain_size.width;
   info.imageExtent.height = swapchain_size.height;
   info.imageArrayLayers = 1;
   info.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
   info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
   info.preTransform = pre_transform;
   info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
   info.presentMode = swapchain_present_mode;
   info.clipped = true;
   info.oldSwapchain = old_swapchain;

   V(vkCreateSwapchainKHR(context->get_device(), &info, nullptr, &swapchain));

   if (old_swapchain != VK_NULL_HANDLE)
      vkDestroySwapchainKHR(context->get_device(), old_swapchain, nullptr);

   width = swapchain_size.width;
   height = swapchain_size.height;
   this->format = format.format;

   LOG("Created swapchain %u x %u (fmt: %u).\n",
         width, height, static_cast<unsigned>(this->format));

   uint32_t image_count;
   V(vkGetSwapchainImagesKHR(context->get_device(), swapchain, &image_count, nullptr));
   swapchain_images.resize(image_count);
   V(vkGetSwapchainImagesKHR(context->get_device(), swapchain, &image_count, swapchain_images.data()));

   return true;
}

WSI::~WSI()
{
   if (context)
   {
      vkDeviceWaitIdle(context->get_device());
      if (swapchain != VK_NULL_HANDLE)
         vkDestroySwapchainKHR(context->get_device(), swapchain, nullptr);
   }

   if (window)
      glfwDestroyWindow(window);

   if (surface != VK_NULL_HANDLE)
      vkDestroySurfaceKHR(context->get_instance(), surface, nullptr);
}
}

