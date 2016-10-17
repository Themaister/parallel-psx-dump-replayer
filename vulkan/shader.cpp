#include "shader.hpp"

namespace Vulkan
{
   PipelineLayout::PipelineLayout(VkDevice device, VkPipelineLayout pipe_layout, const CombinedResourceLayout &layout)
      : device(device), pipe_layout(pipe_layout), layout(layout)
   {
   }

   PipelineLayout::~PipelineLayout()
   {
      vkDestroyPipelineLayout(device, pipe_layout, nullptr);
   }
}
