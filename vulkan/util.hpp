#pragma once

#include <stdint.h>

namespace Vulkan
{
#ifdef __GNUC__
#define leading_zeroes __builtin_clz
#else
#error "Implement me."
#endif

template <typename T>
static inline void for_each_bit(uint32_t value, const T &func)
{
	while (value)
	{
		uint32_t bit = leading_zeroes(value);
		func(bit);
		value &= ~bit;
	}
}
}
