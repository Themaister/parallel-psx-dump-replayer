#pragma once
#include <memory>
#include <stdint.h>
#include <unordered_map>

namespace Vulkan
{
using Hash = uint64_t;
template <typename T>
using HashMap = std::unordered_map<Hash, T>;

class Hasher
{
public:
	template <typename T>
	void data(const T *data, size_t size)
	{
		using arith_type = decltype(data[int()]);
		size /= sizeof(arith_type);
		for (size_t i = 0; i < size; i++)
			h = (h * 0x100000001b3ull) ^ data[i];
	}

	Hash get() const
	{
		return h;
	}

private:
	Hash h = 0xcbf29ce484222325ull;
};
}
