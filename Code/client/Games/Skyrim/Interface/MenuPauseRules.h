#pragma once

#include <Games/EngineNames.h>

#include <atomic>
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

// A live lockpicking menu gets its kUpdate from Job_UI (39042), off the main thread, where 51950 never builds its 3D.
inline bool NeedsMainThreadBuild(std::string_view aName, uint32_t aFlags, bool aBuilt) noexcept
{
    return !aBuilt && !(aFlags & 0x1) && SameName(aName, "Lockpicking Menu");
}

// One main-thread build queued at a time (LiveMenuHooks): an unbuilt kUpdate asks, the queued task's run ends it.
struct MainThreadBuild
{
    [[nodiscard]] bool Ask(std::string_view aName, uint32_t aFlags, bool aBuilt) noexcept
    {
        return NeedsMainThreadBuild(aName, aFlags, aBuilt) && !m_queued.exchange(true);
    }
    void Ran() noexcept { m_queued = false; }

  private:
    std::atomic<bool> m_queued{false};
};

inline uint32_t Flags(std::string_view aName, uint32_t aFlags, bool aInSession) noexcept
{
    return aInSession && RunsInSession(aName) ? aFlags & ~kPauseFlags : aFlags;
}
} // namespace MenuPauseRules
