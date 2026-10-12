#pragma once

#include "EngineCalls.h"

#include <chrono>
#include <optional>
#include <string_view>

namespace TestHarness
{
// The player's buttons as the harness presses them ("input" command). A held button is sent again every
// frame with the time held so far, as its device does (melee needs it).
class PlayerInput
{
public:
    // "right", "left" or "ready".
    static std::optional<EngineCalls::Button> ButtonByName(std::string_view aName) noexcept;
    // Presses or releases the button through the game's input dispatch. The seconds it was held (0 for a
    // press), or nothing if the input system is not up.
    std::optional<float> Set(EngineCalls::Button aButton, bool aDown) noexcept;
    // Every frame once in the world.
    void Poll() noexcept;
    // The player's draw and sheathe animations so far, counted every frame: one can start and end between
    // two status reads (a spell's lasts a third of a second), and a press during one is ignored.
    [[nodiscard]] uint32_t EquipAnimations() const noexcept { return m_equipAnimations; }

private:
    struct HeldButton
    {
        bool Down = false;
        std::chrono::steady_clock::time_point Pressed{};
    };
    HeldButton m_buttons[3]{};
    bool m_equipping = false;
    uint32_t m_equipAnimations = 0;
};
} // namespace TestHarness
