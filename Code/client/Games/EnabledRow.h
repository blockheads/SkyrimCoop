#pragma once

#include <cstdint>

// The kEnabled row's value (ReferenceStates): a reference's form flags a script changes. Disabled (TESForm::SetDisabled
// 14646), deleted (SetDelete 19565) and destroyed (0x800000, TESForm::SetDestroyed 14649: a tripped tripwire).
namespace EnabledRow
{
struct State
{
    bool Enabled = true;
    bool Deleted = false;
    bool Destroyed = false;
};

[[nodiscard]] constexpr uint32_t Pack(const State& acState) noexcept
{
    return (acState.Enabled ? 1u : 0u) | (acState.Deleted ? 2u : 0u) | (acState.Destroyed ? 4u : 0u);
}

[[nodiscard]] constexpr State Unpack(uint32_t aValue) noexcept
{
    return {.Enabled = (aValue & 1) != 0, .Deleted = (aValue & 2) != 0, .Destroyed = (aValue & 4) != 0};
}
} // namespace EnabledRow
