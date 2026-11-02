#pragma once

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

constexpr bool CloseEndsConversation(bool aRunsWithoutMenu, bool aBartering, bool aConversation) noexcept
{
    return !aBartering && (!aRunsWithoutMenu || !aConversation);
}
}
