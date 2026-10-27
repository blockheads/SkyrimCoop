#pragma once

#include <Followers/Anchors.h>

namespace Followers
{
struct State
{
    uint32_t Reference{}; // native reference in the host's world and co-save
    Place At;
    bool Waiting{};
    bool Dead{};
    uint32_t Session{};
};

enum class Command
{
    Recruit,
    Follow,
    Wait,
    Dismiss,
    Trade
};
struct Order
{
    uint32_t Reference{};
    uint32_t Character{}; // the picker's session character
    Command What{};
};
struct Target
{
    uint32_t Reference{};
    uint32_t Character{};
    bool operator==(const Target&) const noexcept = default;
};
} // namespace Followers
