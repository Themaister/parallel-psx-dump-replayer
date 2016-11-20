#pragma once

#include "intrusive.hpp"
#include "vulkan.hpp"

namespace Vulkan
{
class Device;

class Semaphore : public IntrusivePtrEnabled<Semaphore>
{
public:
	Semaphore(Device *device, VkSemaphore semaphore)
	    : device(device)
	    , semaphore(semaphore)
	{
	}

	~Semaphore();

	const VkSemaphore &get_semaphore() const
	{
		return semaphore;
	}

	bool is_signalled() const
	{
		return signalled;
	}

	void signal()
	{
		signalled = true;
	}

	VkSemaphore consume()
	{
		auto ret = semaphore;
		semaphore = VK_NULL_HANDLE;
		VK_ASSERT(signalled);
		signalled = false;
		return ret;
	}

private:
	Device *device;
	VkSemaphore semaphore;
	bool signalled = false;
};

using SemaphoreHandle = IntrusivePtr<Semaphore>;
}
