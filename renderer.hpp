#pragma once

#include "vulkan/vulkan.hpp"
#include "vulkan/wsi.hpp"
#include "vulkan/device.hpp"

namespace PSX
{
class Renderer
{
public:
	Renderer(Vulkan::Device &device);
	~Renderer();

private:
	Vulkan::Device &device;
};

}