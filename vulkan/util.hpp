#pragma once

#include <stdint.h>

namespace Vulkan
{
#ifdef __GNUC__
#define leading_zeroes __builtin_clz
#define trailing_zeroes __builtin_ctz
#define trailing_ones(x) __builtin_ctz(~(x))
#else
#error "Implement me."
#endif

template <typename T>
inline void for_each_bit(uint32_t value, const T &func)
{
	while (value)
	{
		uint32_t bit = trailing_zeroes(value);
		func(bit);
		value &= ~(1u << bit);
	}
}

template <typename T>
inline void for_each_bit_range(uint32_t value, const T &func)
{
	while (value)
	{
		uint32_t bit = trailing_zeroes(value);
		uint32_t range = trailing_ones(value >> bit);
		func(bit, range);
		value &= ~((1u << (bit + range)) - 1);
	}
}
}
