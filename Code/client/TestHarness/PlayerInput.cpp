#include <TiltedOnlinePCH.h>
#include "PlayerInput.h"

#include <algorithm>

namespace TestHarness
{
namespace
{
// In EngineCalls::Button order.
constexpr std::string_view cButtonNames[] = {"right", "left", "ready", "sneak", "forward", "shout"};
} // namespace

std::optional<EngineCalls::Button> PlayerInput::ButtonByName(std::string_view aName) noexcept
{
    const auto cFound = std::find(std::begin(cButtonNames), std::end(cButtonNames), aName);
    if (cFound == std::end(cButtonNames))
        return std::nullopt;
    return static_cast<EngineCalls::Button>(cFound - std::begin(cButtonNames));
}

std::optional<float> PlayerInput::Set(EngineCalls::Button aButton, bool aDown) noexcept
{
    auto& button = m_buttons[static_cast<size_t>(aButton)];
    const auto cNow = std::chrono::steady_clock::now();
    const float cHeld = aDown ? 0.f : std::chrono::duration<float>(cNow - button.Pressed).count();
    if (!EngineCalls::SendPlayerButton(aButton, aDown, cHeld))
        return std::nullopt;
    button.Down = aDown;
    if (aDown)
        button.Pressed = cNow;
    return cHeld;
}

void PlayerInput::Poll() noexcept
{
    // A held button, as its device reports it: every frame, value 1, the time held so far.
    const auto cNow = std::chrono::steady_clock::now();
    for (size_t i = 0; i < std::size(m_buttons); ++i)
        if (m_buttons[i].Down)
            EngineCalls::SendPlayerButton(static_cast<EngineCalls::Button>(i), true, std::max(std::chrono::duration<float>(cNow - m_buttons[i].Pressed).count(), 0.001f));

    const bool cEquipping = EngineCalls::GetGraphBool(0x14, "IsEquipping") || EngineCalls::GetGraphBool(0x14, "IsUnequipping");
    if (cEquipping && !m_equipping)
        ++m_equipAnimations;
    m_equipping = cEquipping;
}
} // namespace TestHarness
