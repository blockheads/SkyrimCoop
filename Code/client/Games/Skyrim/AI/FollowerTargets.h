#pragma once

#include <Followers/State.h>
#include <vector>

namespace FollowerTargets
{
// Active only for the host's engine. Native hooks collect commands and resolve the vanilla follow target.
[[nodiscard]] bool OpenInventory(uint32_t aReference) noexcept;
void Start() noexcept;
void Stop() noexcept;
[[nodiscard]] std::vector<Followers::Order> TakeOrders() noexcept;
void Set(const std::vector<Followers::Target>& acTargets) noexcept;
} // namespace FollowerTargets
