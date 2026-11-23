#include <catch2/catch.hpp>

#include <Interface/MenuPauseRules.h>

TEST_CASE("Personal menus leave the session running and keep their input and saving flags", "[menus]")
{
    constexpr uint32_t cFlags = 0xFFFFFFFF;
    for (const auto cMenu : {"MapMenu", "Journal Menu", "Crafting Menu", "Lockpicking Menu",
                             "BarterMenu", "Book Menu", "Training Menu", "InventoryMenu", "Console"})
    {
        INFO(cMenu);
        CHECK(MenuPauseRules::Flags(cMenu, cFlags, true) == (cFlags & ~MenuPauseRules::kPauseFlags));
        CHECK(MenuPauseRules::Flags(cMenu, cFlags, false) == cFlags);
    }
}

TEST_CASE("Shared rest and loads retain their pauses", "[menus]")
{
    for (const auto cMenu : {"Sleep/Wait Menu", "Loading Menu", "Main Menu", "Unknown Menu"})
    {
        INFO(cMenu);
        CHECK(MenuPauseRules::Flags(cMenu, 0xFFFFFFFF, true) == 0xFFFFFFFF);
    }
}

TEST_CASE("Menu names retain the engine's case-insensitive pooling", "[menus]")
{
    CHECK(MenuPauseRules::Flags("mapmenu", MenuPauseRules::kPauseFlags, true) == 0);
    CHECK(MenuPauseRules::Flags("JOURNAL MENU", MenuPauseRules::kPauseFlags, true) == 0);
}

TEST_CASE("A live lockpicking menu gets its 3D built on the main thread until it is built", "[menus]")
{
    const uint32_t cLive = MenuPauseRules::Flags("Lockpicking Menu", 0xFFFFFFFF, true);
    CHECK(MenuPauseRules::NeedsMainThreadBuild("Lockpicking Menu", cLive, false));
    CHECK_FALSE(MenuPauseRules::NeedsMainThreadBuild("Lockpicking Menu", cLive, true));
    // Paused (no session): the engine's own main-thread kUpdate builds it.
    CHECK_FALSE(MenuPauseRules::NeedsMainThreadBuild("Lockpicking Menu", 0xFFFFFFFF, false));
    CHECK_FALSE(MenuPauseRules::NeedsMainThreadBuild("Journal Menu", 0, false));
}

TEST_CASE("A lockpicking menu's main-thread build is queued once until it runs", "[menus]")
{
    const uint32_t cLive = MenuPauseRules::Flags("Lockpicking Menu", 0xFFFFFFFF, true);
    MenuPauseRules::MainThreadBuild build;
    CHECK(build.Ask("Lockpicking Menu", cLive, false));
    CHECK_FALSE(build.Ask("Lockpicking Menu", cLive, false)); // Job_UI's next kUpdate before the main loop ran it
    build.Ran();
    CHECK_FALSE(build.Ask("Lockpicking Menu", cLive, true));
    CHECK(build.Ask("Lockpicking Menu", cLive, false)); // a later menu, not built yet
}
