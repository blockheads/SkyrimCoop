#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>

// The motion type a script last gave each reference (ObjectReference.SetMotionType, 56232 -> 56332): saved with it as
// a havok change, read by the host for its world row (kMotion; physics.md, "A script's motion type").
namespace ScriptMotion
{
// A row's value: the Papyrus motion type (1 dynamic .. 4 keyframed, 6) and its allowActivate in bit 8.
[[nodiscard]] constexpr uint32_t Pack(uint32_t aType, bool aAllowActivate) noexcept
{
    return (aType & 0xFF) | (aAllowActivate ? 0x100u : 0u);
}
[[nodiscard]] constexpr uint32_t TypeOf(uint32_t aValue) noexcept
{
    return aValue & 0xFF;
}
[[nodiscard]] constexpr bool AllowsActivate(uint32_t aValue) noexcept
{
    return (aValue & 0x100) != 0;
}

struct Store
{
    void Remember(uint32_t aFormId, uint32_t aValue) noexcept
    {
        std::scoped_lock _(m_lock);
        m_values[aFormId] = aValue;
    }
    [[nodiscard]] std::optional<uint32_t> Of(uint32_t aFormId) const noexcept
    {
        std::scoped_lock _(m_lock);
        const auto it = m_values.find(aFormId);
        return it != m_values.end() ? std::optional(it->second) : std::nullopt;
    }

private:
    mutable std::mutex m_lock;
    std::unordered_map<uint32_t, uint32_t> m_values;
};

// This game's.
Store& Get() noexcept;
} // namespace ScriptMotion
