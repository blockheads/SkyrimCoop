#pragma once

#include <cstdint>

// Which saves the Journal's Load lists (docs/re/systems/save-game.md, "Only this playthrough's saves"). No engine.
namespace SaveList
{
enum class Limit : uint8_t
{
    kAll,       // the main menu: every playthrough, as vanilla
    kCurrent,   // in a game: the loaded playthrough's character only
    kNothing,   // a guest in a session: it loads nothing itself
};

[[nodiscard]] constexpr Limit For(bool aAtMainMenu, bool aGuestInSession) noexcept
{
    if (aGuestInSession)
        return Limit::kNothing;
    return aAtMainMenu ? Limit::kAll : Limit::kCurrent;
}

// Whether a save or a character row of aCharacter is listed (kAllCharacters: the "Show all saves" row).
inline constexpr uint32_t kAllCharacters = 0xFFFFFFFF;
[[nodiscard]] constexpr bool Shows(Limit aLimit, uint32_t aCurrent, uint32_t aCharacter) noexcept
{
    switch (aLimit)
    {
    case Limit::kAll: return true;
    case Limit::kCurrent: return aCharacter == aCurrent;
    case Limit::kNothing: return false;
    }
    return false;
}
} // namespace SaveList
