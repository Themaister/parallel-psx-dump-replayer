#pragma once

#include "vulkan.hpp"
#include "limits.hpp"
#include "intrusive.hpp"

namespace Vulkan
{
   class Device;

   enum class ShaderStage
   {
      Vertex = 0,
      TessControl = 1,
      TessEvaluation = 2,
      Geometry = 3,
      Fragment = 4,
      Compute = 5,
      Count
   };

   struct DescriptorSetLayout
   {
      uint32_t sampled_image_mask = 0;
      uint32_t storage_image_mask = 0;
      uint32_t uniform_buffer_mask = 0;
      uint32_t storage_buffer_mask = 0;
   };

   struct ResourceLayout
   {
      uint32_t attribute_mask = 0;
      uint32_t push_constant_offset = 0;
      uint32_t push_constant_range = 0;
      DescriptorSetLayout sets[VULKAN_NUM_DESCRIPTOR_SETS]; 
   };

   struct CombinedResourceLayout
   {
      uint32_t attribute_mask = 0;
      DescriptorSetLayout sets[VULKAN_NUM_DESCRIPTOR_SETS]; 
      VkPushConstantRange ranges[static_cast<unsigned>(ShaderStage::Count)] = {};
   };

   class DescriptorSetAllocator;
   class PipelineLayout
   {
      public:
         PipelineLayout(VkDevice device, VkPipelineLayout pipe_layout, const CombinedResourceLayout &layout);
         ~PipelineLayout();

         void set_allocator(unsigned set, DescriptorSetAllocator *set_allocator)
         {
            set_allocators[set] = set_allocator;
         }

      private:
         VkDevice device;
         VkPipelineLayout pipe_layout;
         CombinedResourceLayout layout;
         DescriptorSetAllocator *set_allocators[VULKAN_NUM_DESCRIPTOR_SETS] = {};
   };

   class Shader
   {
      public:
         Shader(Device *device, VkShaderModule module);
         ~Shader();

         const ResourceLayout &get_layout() const
         {
            return layout;
         }

      private:
         Device *device;
         VkShaderModule module;
         ResourceLayout layout;
   };
   using ShaderHandle = IntrusivePtr<Shader>;

   class Program
   {
      public:
         void set_shader(ShaderStage stage, ShaderHandle handle);
         inline const Shader *get_shader(ShaderStage stage) const
         {
            return shaders[static_cast<unsigned>(stage)].get();
         }

         void set_pipeline_layout(const PipelineLayout *new_layout)
         {
            layout = new_layout;
         }

      private:
         ShaderHandle shaders[static_cast<unsigned>(ShaderStage::Count)];
         const PipelineLayout *layout = nullptr;
   };
}

