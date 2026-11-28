#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

// The engine's save file names (docs/re/systems/save-game.md, "The character a save belongs to"). No engine.
namespace SaveNames
{
// Our scratch saves in the engine's Saves folder (a join's load, a host load's, a transfer's image): never a player's.
// Each is deleted once used, or main menu Continue would load it (save-game.md, "Continue").
inline constexpr const char* kJoin = "SkyrimCoopJoin";
inline constexpr const char* kReload = "SkyrimCoopWipe";
inline constexpr const char* kTransfer = "SkyrimCoopTransfer";
inline constexpr std::array kScratch{kJoin, kReload, kTransfer};

// The character a load of acName gives the engine, {id, modded} as BSSaveDataSystemUtility::Entry's parse (109348)
// reads it: Save|Autosave|Quicksave<N>_<id hex>_<modded>_<name hex>_<place>_<time>_<date>_<x>_1, 9 fields. Any other
// name: {0, false}. The engine compares both (CharacterIdPair, 35749 recomputes the modded flag on every save).
struct Character
{
    uint32_t Id{};
    bool Modded{};
    bool operator==(const Character&) const = default;
};

[[nodiscard]] inline Character CharacterOf(std::string_view aName) noexcept
{
    if (aName.ends_with(".ess"))
        aName.remove_suffix(4);
    std::vector<std::string_view> fields;
    for (size_t start = 0;;)
    {
        const size_t cEnd = aName.find('_', start);
        fields.push_back(aName.substr(start, cEnd == std::string_view::npos ? std::string_view::npos : cEnd - start));
        if (cEnd == std::string_view::npos)
            break;
        start = cEnd + 1;
    }
    const auto cNumber = [](std::string_view aField, int aBase) {
        uint64_t value = 0;
        for (const char c : aField)
        {
            const int cDigit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
            if (cDigit >= aBase)
                break;
            value = value * aBase + cDigit;
        }
        return static_cast<uint32_t>(value);
    };
    const auto cStarts = [](std::string_view aField, std::string_view aPrefix) {
        if (aField.size() < aPrefix.size())
            return false;
        for (size_t i = 0; i < aPrefix.size(); ++i)
            if ((aField[i] | 0x20) != (aPrefix[i] | 0x20))
                return false;
        return true;
    };
    if (fields.size() != 9 || cNumber(fields.back(), 10) != 1)
        return {};
    if (!cStarts(fields[0], "Autosave") && !cStarts(fields[0], "Save") && !cStarts(fields[0], "Quicksave"))
        return {};
    return {cNumber(fields[1], 16), cNumber(fields[2], 10) != 0};
}

[[nodiscard]] inline uint32_t CharacterId(std::string_view aName) noexcept
{
    return CharacterOf(aName).Id;
}
} // namespace SaveNames
