#include "semaphore.hpp"
#include "device.hpp"

namespace Vulkan
{
Semaphore::~Semaphore()
{
	if (semaphore)
		device->destroy_semaphore(semaphore);
}
}
