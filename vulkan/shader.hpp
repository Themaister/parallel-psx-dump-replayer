#pragma once

#include "cookie.hpp"
#include "descriptor_set.hpp"
#include "hashmap.hpp"
#include "intrusive.hpp"
#include "limits.hpp"
#include "vulkan.hpp"

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

class PipelineLayout : public Cookie
{
public:
	PipelineLayout(Device *device, const CombinedResourceLayout &layout);
	~PipelineLayout();

	const CombinedResourceLayout &get_resource_layout() const
	{
		return layout;
	}

	VkPipelineLayout get_layout() const
	{
		return pipe_layout;
	}

	DescriptorSetAllocator *get_allocator(unsigned set) const
	{
		return set_allocators[set];
	}

private:
	Device *device;
	VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
	CombinedResourceLayout layout;
	DescriptorSetAllocator *set_allocators[VULKAN_NUM_DESCRIPTOR_SETS] = {};
};

class Shader : public IntrusivePtrEnabled<Shader>
{
public:
	Shader(VkDevice device, ShaderStage stage, const uint32_t *data, size_t size);
	~Shader();

	const ResourceLayout &get_layout() const
	{
		return layout;
	}

	ShaderStage get_stage() const
	{
		return stage;
	}

private:
	VkDevice device;
	ShaderStage stage;
	VkShaderModule module;
	ResourceLayout layout;
};
using ShaderHandle = IntrusivePtr<Shader>;

class Program : public IntrusivePtrEnabled<Program>, public Cookie
{
public:
	Program(Device *device);
	~Program();

	void set_shader(ShaderHandle handle);
	inline const Shader *get_shader(ShaderStage stage) const
	{
		return shaders[static_cast<unsigned>(stage)].get();
	}

	void set_pipeline_layout(PipelineLayout *new_layout)
	{
		layout = new_layout;
	}

	PipelineLayout *get_pipeline_layout() const
	{
		return layout;
	}

	VkPipeline get_pipeline(Hash hash);
	void add_pipeline(Hash hash, VkPipeline pipeline);

private:
	Device *device;
	ShaderHandle shaders[static_cast<unsigned>(ShaderStage::Count)];
	PipelineLayout *layout = nullptr;
	HashMap<VkPipeline> pipelines;
};
using ProgramHandle = IntrusivePtr<Program>;
}
