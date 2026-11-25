#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

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
        m_values[aFormId] = {aValue, true};
    }
    [[nodiscard]] std::optional<uint32_t> Of(uint32_t aFormId) const noexcept
    {
        std::scoped_lock _(m_lock);
        const auto it = m_values.find(aFormId);
        return it != m_values.end() ? std::optional(it->second.Value) : std::nullopt;
    }
    // Once for each script call: its live row (a moving body's reference is read every frame).
    [[nodiscard]] std::optional<uint32_t> TakeUnsent(uint32_t aFormId) noexcept
    {
        std::scoped_lock _(m_lock);
        const auto it = m_values.find(aFormId);
        if (it == m_values.end() || !std::exchange(it->second.Unsent, false))
            return std::nullopt;
        return it->second.Value;
    }

    // A save is loading: its scripts set their types again (OnCellAttach); a type from another save must not stand.
    void Clear() noexcept
    {
        std::scoped_lock _(m_lock);
        m_values.clear();
        m_deferred.clear();
    }
    // A guest's row for a reference with no 3D yet: applied when its 3D loads (TESObjectLoadedEvent).
    void Defer(uint32_t aFormId, uint32_t aValue) noexcept
    {
        std::scoped_lock _(m_lock);
        m_deferred[aFormId] = aValue;
    }
    [[nodiscard]] std::optional<uint32_t> TakeDeferred(uint32_t aFormId) noexcept
    {
        std::scoped_lock _(m_lock);
        const auto it = m_deferred.find(aFormId);
        if (it == m_deferred.end())
            return std::nullopt;
        const uint32_t cValue = it->second;
        m_deferred.erase(it);
        return cValue;
    }

private:
    struct Entry
    {
        uint32_t Value{};
        bool Unsent{};
    };
    mutable std::mutex m_lock;
    std::unordered_map<uint32_t, Entry> m_values;
    std::unordered_map<uint32_t, uint32_t> m_deferred;
};

// This game's.
Store& Get() noexcept;
} // namespace ScriptMotion
