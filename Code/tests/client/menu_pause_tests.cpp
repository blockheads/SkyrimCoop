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
