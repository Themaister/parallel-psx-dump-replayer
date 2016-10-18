#pragma once
#include <memory>
#include <stdint.h>
#include <unordered_map>

namespace Vulkan
{
using Hash = uint64_t;
template <typename T>
using HashMap = std::unordered_map<Hash, T>;
}
