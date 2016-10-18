#include "shader.hpp"
#include "device.hpp"
#include "spirv_cross.hpp"

using namespace std;
using namespace spirv_cross;

namespace Vulkan
{
PipelineLayout::PipelineLayout(VkDevice device, VkPipelineLayout pipe_layout, const CombinedResourceLayout &layout)
    : device(device)
    , pipe_layout(pipe_layout)
    , layout(layout)
{
}

PipelineLayout::~PipelineLayout()
{
	vkDestroyPipelineLayout(device, pipe_layout, nullptr);
}

Shader::Shader(VkDevice device, ShaderStage stage, const uint32_t *data, VkDeviceSize size)
    : device(device)
    , stage(stage)
{
	VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	info.codeSize = size;
	info.pCode = data;

	if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
		LOG("Failed to create shader module.\n");

	vector<uint32_t> code(data, data + size / sizeof(uint32_t));
	Compiler compiler(move(code));

	auto resources = compiler.get_shader_resources();
	for (auto &image : resources.sampled_images)
	{
		auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		layout.sets[set].sampled_image_mask |= 1u << binding;
		layout.sets[set].stages |= 1u << static_cast<unsigned>(stage);
	}

	for (auto &image : resources.storage_images)
	{
		auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		layout.sets[set].storage_image_mask |= 1u << binding;
		layout.sets[set].stages |= 1u << static_cast<unsigned>(stage);
	}

	for (auto &buffer : resources.uniform_buffers)
	{
		auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
		layout.sets[set].uniform_buffer_mask |= 1u << binding;
		layout.sets[set].stages |= 1u << static_cast<unsigned>(stage);
	}

	for (auto &buffer : resources.storage_buffers)
	{
		auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
		layout.sets[set].storage_buffer_mask |= 1u << binding;
		layout.sets[set].stages |= 1u << static_cast<unsigned>(stage);
	}

	if (stage == ShaderStage::Vertex)
	{
		for (auto &attrib : resources.stage_inputs)
		{
			auto location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
			layout.attribute_mask |= 1u << location;
		}
	}

	if (!resources.push_constant_buffers.empty())
	{
		auto ranges = compiler.get_active_buffer_ranges(resources.push_constant_buffers.front().id);
		size_t minimum = ~0u;
		size_t maximum = 0;
		if (!ranges.empty())
		{
			for (auto &range : ranges)
			{
				minimum = min(minimum, range.offset);
				maximum = max(maximum, range.offset + range.range);
			}
			layout.push_constant_offset = minimum;
			layout.push_constant_range = maximum - minimum;
		}
	}
}

Shader::~Shader()
{
	if (module)
		vkDestroyShaderModule(device, module, nullptr);
}

void Program::set_shader(ShaderHandle handle)
{
	shaders[static_cast<unsigned>(handle->get_stage())] = handle;
}

Program::Program(Device *device)
    : device(device)
{
}

Program::~Program()
{
	for (auto &pipe : pipelines)
		device->destroy_pipeline(pipe.second);
}
}
