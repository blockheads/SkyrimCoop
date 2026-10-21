#pragma once

#include <cstdint>

// On the host, every guest's character is the player in scripts, and Game.GetPlayer() is the guest whose
// action started the script (docs/design/party-model.md, "A guest's actions on the host").
namespace GuestScripts
{
// An actor became a guest's character (aIsGuest) or stopped being one; kept by SkyrimBridge.
void SetGuest(uint32_t aFormId, bool aIsGuest) noexcept;
// The guest a native running on this thread runs for (a script they started), or 0.
uint32_t NativeGuest() noexcept;
} // namespace GuestScripts
