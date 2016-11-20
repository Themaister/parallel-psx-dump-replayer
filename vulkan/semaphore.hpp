#pragma once

#include "intrusive.hpp"
#include "vulkan.hpp"

namespace Vulkan
{
class Device;

class SemaphoreHolder : public IntrusivePtrEnabled<SemaphoreHolder>
{
public:
	SemaphoreHolder(Device *device, VkSemaphore semaphore)
	    : device(device)
	    , semaphore(semaphore)
	{
	}

	~SemaphoreHolder();

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

using Semaphore = IntrusivePtr<SemaphoreHolder>;
}
