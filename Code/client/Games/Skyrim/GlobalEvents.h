#pragma once

#include <cstdint>
#include <set>

namespace GlobalEvents
{
void Start() noexcept;
void Stop() noexcept;
std::set<uint32_t> Take() noexcept;
} // namespace GlobalEvents
