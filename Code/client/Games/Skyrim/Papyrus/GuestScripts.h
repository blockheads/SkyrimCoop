#pragma once

#include <cstdint>
#include <string_view>

// On the host, every guest's character is the player in scripts, and Game.GetPlayer() is the guest whose
// action started the script (docs/design/party-model.md, "A guest's actions on the host"). On a guest, the
// scripts its own player caused (docs/design/world-state-sync.md, "Only the host's game decides").
namespace GuestScripts
{
// An actor became a guest's character (aIsGuest) or stopped being one; kept by SkyrimBridge.
void SetGuest(uint32_t aFormId, bool aIsGuest) noexcept;
// The guest a native running on this thread runs for (a script they started), or 0. This game's player (0x14) for
// a script it caused: always on a guest, in a session on the host.
uint32_t NativeGuest() noexcept;
// Whether a native is running on this thread (what is queued now is its script's, not the engine's event).
bool InNative() noexcept;

// While it lives, the calls this thread queues on scripts run for aPlayer (an event sent on their behalf).
class ScopedCause
{
public:
    explicit ScopedCause(uint32_t aPlayer) noexcept;
    ~ScopedCause() noexcept;
    ScopedCause(const ScopedCause&) = delete;
    ScopedCause& operator=(const ScopedCause&) = delete;

private:
    uint32_t m_outer;
};

// The player whose script set the timer or registration that aEvent on aObject (a VM handle) answers, or 0.
uint32_t RegisteredBy(uint64_t aObject, std::string_view aEvent) noexcept;
} // namespace GuestScripts
