#pragma once

#include <cstdint>

namespace DialogueMenuPolicy
{
enum class PickedDisplay { Ignore, Animate, ReplaceText };

constexpr PickedDisplay DisplayPicked(double aMenuState) noexcept
{
    return aMenuState == 1 ? PickedDisplay::Animate : aMenuState == 2 ? PickedDisplay::ReplaceText : PickedDisplay::Ignore;
}

constexpr bool NeedsRefill(bool aOptionsChanged, bool aListShown, bool aResuming = false) noexcept
{
    return aResuming || aOptionsChanged || !aListShown;
}

// The speaker's body would not turn only for the engine's rule that, talking to this game's player, it turns to that
// player alone (Character::ShouldRotateToTrack 38011): its target is the talker the host names instead.
constexpr bool AskTurnForTalker(bool aTurns, bool aTalkingToPlayer, bool aSpeaker, uint32_t aTarget, uint32_t aTalker) noexcept
{
    return !aTurns && aTalkingToPlayer && aSpeaker && aTalker && aTarget == aTalker;
}

constexpr bool CloseEndsConversation(bool aRunsWithoutMenu, bool aBartering, bool aConversation) noexcept
{
    return !aBartering && (!aRunsWithoutMenu || !aConversation);
}
}
