#pragma once

#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Wire/Buffer.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief What a save keeps of its player beside the character (docs/re/systems/player-save-state.md): favorites, damage,
 * timed effects, power cooldowns, trainings, its save globals. A guest's record carries its own.
 */
struct PlayerOwnState
{
    // A favorited stack (ExtraHotkey 0x49) as the record's Items list it, and its number key: -1 none, 0-7.
    struct FavoriteItem
    {
        Inventory::Entry Stack{};
        int8_t Hotkey{-1};
        bool operator==(const FavoriteItem&) const = default;
    };

    // A timed effect on the player: its spell, its effect's index in it, the seconds it ran of its duration.
    struct Effect
    {
        GameId Spell{};
        uint8_t Index{};
        float Elapsed{};
        float Duration{};
        bool operator==(const Effect&) const = default;
    };

    // A power's cooldown (Actor+0x188): real seconds left.
    struct Cooldown
    {
        GameId Spell{};
        float Seconds{};
        bool operator==(const Cooldown&) const = default;
    };

    // The save globals that are the player's (BGSSaveLoadManager::SaveGlobalData 35639, by type).
    static constexpr uint32_t kMiscStats = 0;
    static constexpr uint32_t kMagicFavorites = 109;
    static constexpr uint32_t kPlayerControls = 110;
    static constexpr std::array<uint32_t, 3> kGlobals{kMiscStats, kMagicFavorites, kPlayerControls};

    // False in a record an older build wrote: nothing to apply.
    bool Present{};
    std::vector<FavoriteItem> FavoriteItems;
    std::map<uint32_t, std::string> Globals; // type -> the engine's own bytes for it
    std::array<float, 3> Damage{};           // health, magicka, stamina damage modifiers (0 or less)
    std::vector<Effect> Effects;
    std::vector<Cooldown> Cooldowns;
    uint32_t Trainings{};
    bool FirstPerson{};

    bool operator==(const PlayerOwnState&) const = default;

    // aSeconds of play later: effects ran longer, cooldowns counted down, the finished ones gone.
    void Age(float aSeconds) noexcept;
    // The engine's loader for aType reads acData within its bytes (it checks no bounds: 36004, 36002, 36000 ours).
    [[nodiscard]] static bool GlobalReadable(uint32_t aType, std::string_view acData) noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;
};
