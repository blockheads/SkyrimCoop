#pragma once

#include <optional>

namespace Ui
{
// Primitive observations from Skyrim, also replayed by the native workbench.
struct HudVisibility
{
    bool MenuSystemVisible = false;
    bool HudMenuOpen = false;
    bool MovieReady = false;
    bool BlockingMenu = false;
    std::optional<bool> RootVisible;
    std::optional<bool> HudClipVisible;
    std::optional<bool> HealthClipVisible;

    [[nodiscard]] bool AllowsParty() const
    {
        return MenuSystemVisible && HudMenuOpen && MovieReady && !BlockingMenu && RootVisible.value_or(true) &&
               HudClipVisible.value_or(true) && HealthClipVisible.value_or(true);
    }
};
} // namespace Ui
