#pragma once

// CrimeBridge::Judge's rule with no engine (docs/re/systems/crime.md): checked by ClientTests; the bridge and
// GameTests' fake both follow it.
#include <Messages/CrimeMessages.h>

namespace CrimeRules
{
// Whether the host asks if the party owns the crime's target (IsAnOwner 20210) before judging. Not a pickpocket (its
// alarm decides), nor a theft naming an actor: for a created item the engine names its owner actor
// (PlayerCharacter::PickUpObject 40533), and an actor reference always reads as the player's.
[[nodiscard]] constexpr bool RechecksOwnership(CrimeAction aAction, bool aTargetIsActor) noexcept
{
    if (aAction == CrimeAction::Pickpocket)
        return false;
    return !(aAction == CrimeAction::Theft && aTargetIsActor);
}
} // namespace CrimeRules
