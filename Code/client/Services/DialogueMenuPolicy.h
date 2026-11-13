#pragma once

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

constexpr bool CloseEndsConversation(bool aRunsWithoutMenu, bool aBartering, bool aConversation) noexcept
{
    return !aBartering && (!aRunsWithoutMenu || !aConversation);
}
}
