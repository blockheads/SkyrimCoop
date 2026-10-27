#pragma once

#include <Games/EngineNames.h>

#include <cstdint>
#include <string_view>

namespace MenuPauseRules
{
constexpr uint32_t kPauseFlags = 0x1 | 0x20 | 0x1000000;

// Shared rest and loading retain their native pauses. Personal menus keep the session running.
inline bool RunsInSession(std::string_view aName) noexcept
{
    constexpr std::string_view cMenus[] = {
        "TweenMenu", "MagicMenu", "StatsMenu", "InventoryMenu", "MessageBoxMenu",
        "ContainerMenu", "FavoritesMenu", "Tutorial Menu", "Console", "MapMenu", "Journal Menu",
        "Crafting Menu", "Lockpicking Menu", "BarterMenu", "Book Menu", "Training Menu"
    };
    for (const auto cMenu : cMenus)
        if (SameName(aName, cMenu))
            return true;
    return false;
}

inline uint32_t Flags(std::string_view aName, uint32_t aFlags, bool aInSession) noexcept
{
    return aInSession && RunsInSession(aName) ? aFlags & ~kPauseFlags : aFlags;
}
} // namespace MenuPauseRules
