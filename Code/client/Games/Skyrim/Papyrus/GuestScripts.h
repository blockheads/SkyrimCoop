#pragma once

#include <cstdint>

// On the host, every guest's character is the player in scripts, and Game.GetPlayer() is the guest whose
// action started the script (docs/design/party-model.md, "A guest's actions on the host").
namespace GuestScripts
{
// An actor became a guest's character (aIsGuest) or stopped being one; kept by SkyrimBridge.
void SetGuest(uint32_t aFormId, bool aIsGuest) noexcept;
} // namespace GuestScripts
