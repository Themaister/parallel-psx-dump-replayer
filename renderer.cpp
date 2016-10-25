#include "renderer.hpp"

using namespace Vulkan;
using namespace std;

namespace PSX
{
Renderer::Renderer(Device &device)
	: device(device)
{
}

Renderer::~Renderer()
{
}

}