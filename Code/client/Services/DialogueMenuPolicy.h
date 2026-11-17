#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace DialogueMenuPolicy
{
// The SWF's eMenuState (DialogueMenu.as): its list shown, or an option clicked while the NPC answers.
constexpr double kTopicListShown = 1;
constexpr double kTopicClicked = 2;

enum class PickedDisplay { Ignore, Animate, ReplaceText };

constexpr PickedDisplay DisplayPicked(double aMenuState) noexcept
{
    return aMenuState == kTopicListShown ? PickedDisplay::Animate
         : aMenuState == kTopicClicked   ? PickedDisplay::ReplaceText
                                         : PickedDisplay::Ignore;
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

// A highlight this game's menu moved (re/systems/dialogue.md, "The mouse"): with the cursor moving it is a hover, this
// menu's own; else W, S or the wheel moved it. A heuristic: the cursor and the SWF's rollover may be a frame apart.
enum class HighlightMove { Send, Local };
constexpr uint32_t kHoverFrames = 2;

constexpr HighlightMove OnHighlightMoved(uint32_t aFramesSinceCursorMoved) noexcept
{
    return aFramesSinceCursorMoved <= kHoverFrames ? HighlightMove::Local : HighlightMove::Send;
}

// A list's first highlight: the menu's fill moved nobody's (told only when not the top), unless the mouse put it there
// (now, or a hover the same step's list refresh kept).
constexpr HighlightMove OnFirstHighlight(uint32_t aIndex, uint32_t aFramesSinceCursorMoved, bool aHovered) noexcept
{
    return aIndex == 0 || aHovered ? HighlightMove::Local : OnHighlightMoved(aFramesSinceCursorMoved);
}

// The rows' rollover held from the bridge's recentring until the cursor moves: a row sliding under a resting cursor
// takes nothing (dialogue.md, "The mouse").
struct RolloverHold
{
    using Cursor = std::optional<std::pair<float, float>>;
    Cursor At;
    bool Held = false;

    void Recentred(const Cursor& acCursor) noexcept
    {
        At = acCursor;
        Held = true;
    }
    // True when the hold ends here (the cursor moved since the recentring).
    bool Release(const Cursor& acCursor) noexcept
    {
        if (!Held || At == acCursor)
            return false;
        Held = false;
        return true;
    }
};

// Asks the engine's first-option cursor (FirstOption 35267, which moves the picked option) leaving the picked option
// as it was: the next list build takes its choices from it (dialogue.md, "A start during the NPC's last line").
template <class TPicked, class TAsk> bool AskKeepingPicked(TPicked& aPicked, TAsk&& aAsk)
{
    const TPicked cPicked = aPicked;
    const bool cAnswer = aAsk();
    aPicked = cPicked;
    return cAnswer;
}

constexpr bool CloseEndsConversation(bool aRunsWithoutMenu, bool aBartering, bool aConversation) noexcept
{
    return !aBartering && (!aRunsWithoutMenu || !aConversation);
}
}
