#pragma once

#include "vulkan.hpp"
#include "wsi.hpp"
#include "device.hpp"

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