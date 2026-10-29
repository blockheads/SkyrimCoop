#pragma once

#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Structs/PlayerKnowledge.h>
#include <Structs/Recipe.h>
#include <Structs/Tints.h>
#include <Wire/Buffer.h>

#include <array>
#include <map>
#include <string>
#include <vector>

/**
 * @brief A player's character as the host's save keeps it (docs/design/join-flow.md, "The character record"): the
 * engine's own data where it keeps the character apart from the world.
 */
struct PlayerRecord
{
    static constexpr size_t kSkillCount = 18; // ActorValue 6 (OneHanded) to 23 (Enchanting)

    // One skill as PlayerSkills keeps it.
    struct Skill
    {
        float Level{};
        float Experience{};
        float Threshold{}; // experience to its next level
        uint32_t Legendary{};
        bool operator==(const Skill&) const = default;
    };

    struct Perk
    {
        GameId Id{};
        uint8_t Rank{1};
        bool operator==(const Perk&) const = default;
    };

    struct Value
    {
        uint8_t Index{}; // ActorValue
        float Base{};
        bool operator==(const Value&) const = default;
    };

    // "steam:<SteamID64>" or "id:<16 hex digits>" (JoinService::PlayerKey).
    std::string Key;
    std::string Name;
    uint16_t Level{1};

    // The player base's save data (FormState of 0x7) for BaseFlags: name, race, sex, face, level, base spells, shouts.
    std::string Base;
    uint32_t BaseFlags{};
    Tints FaceTints{};
    float Weight{50.f}; // TESNPC weight, 0 to 100: saved with the actor (Actor::SaveGame 37649), not in Base

    float LevelExperience{};
    float LevelThreshold{};
    std::array<Skill, kSkillCount> Skills{};
    uint8_t PerkPoints{};
    std::vector<Perk> Perks;
    std::vector<GameId> Spells; // added to the player, beyond its base's
    std::vector<Value> Values;  // base actor values; DragonSouls (133) its current value, each player's own

    Inventory Items{}; // with what is worn and held; gold is an item

    // A blob per mod adapter, by name ("racemenu").
    std::map<std::string, std::string> Extras;

    // Where the host's game last showed this character (none: the host's save has no place for it).
    GameId Cell{};
    GameId WorldSpace{};
    std::array<float, 3> Position{};
    [[nodiscard]] bool HasPlace() const noexcept { return Cell || WorldSpace; }

    // The words of power this player has unlocked: its own, bought with its souls (docs/design/dragonborn.md). A record
    // an older build saved has none.
    std::vector<GameId> Words;
    // Own permanent totals (base + permanent modifier), excluding temporary effects and damage.
    std::vector<Value> PermanentValues;
    PlayerKnowledge Knowledge;
    // The recipes of its player-made potions and poisons (Items' recipe ids), for its game to make them again.
    std::vector<Recipe> Recipes;

    bool operator==(const PlayerRecord& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;
};
