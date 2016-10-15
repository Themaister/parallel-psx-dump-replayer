#pragma once

#include "vulkan_symbol_wrapper.h"
#include "vulkan.hpp"
#include "device.hpp"
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>

namespace Vulkan
{

class WSI
{
   public:
      bool init(unsigned width, unsigned height);
      ~WSI();

      bool alive();
      void update_framebuffer(unsigned width, unsigned height);

      inline VulkanContext &get_context()
      {
         return *context;
      }

      inline Device &get_device()
      {
         return device;
      }

   private:
      std::unique_ptr<VulkanContext> context;
      GLFWwindow *window = nullptr;
      VkSurfaceKHR surface = VK_NULL_HANDLE;
      VkSwapchainKHR swapchain = VK_NULL_HANDLE;
      std::vector<VkImage> swapchain_images;
      Device device;

      unsigned width = 0;
      unsigned height = 0;
      VkFormat format = VK_FORMAT_UNDEFINED;

      bool init_swapchain(unsigned width, unsigned height);
};
}
