#pragma once

#include <array>
#include <cstdint>
#include <optional>

// What a condition about the player becomes when it is judged for a guest's character (JudgedFor): the engine reads
// the player outside its subject and target in these ways (docs/re/systems/conditions.md, "Judged for a character").
// Checked by ClientTests.
namespace JudgedRules
{
// Condition functions (CommonLibSSE-NG FUNCTION_DATA::FunctionID).
inline constexpr uint16_t kGetIsClass = 68;
inline constexpr uint16_t kGetIsRace = 69;
inline constexpr uint16_t kGetIsSex = 70;
inline constexpr uint16_t kGetInFaction = 71;
inline constexpr uint16_t kGetPCIsClass = 129;
inline constexpr uint16_t kGetPCIsRace = 130;
inline constexpr uint16_t kGetPCIsSex = 131;
inline constexpr uint16_t kGetPCInFaction = 132;
// An NPC's relationship with the player is the party's standing (party-model.md: the party is the Dragonborn): left
// as the host's player's when judged for another character.
inline constexpr uint16_t kGetRelationshipRank = 403;

// One condition item, as the hook reads it.
struct Item
{
    uint16_t Function = 0;
    bool OnPlayer = false;                 // it runs on the player: a fixed reference or a quest alias holding it
    std::array<bool, 2> PlayerParam{};     // a parameter that is the player
};

// The item to evaluate in its place.
struct Judged
{
    uint16_t Function = 0;
    bool OnCharacter = false;              // runs on the character (a fixed reference to it)
    std::array<bool, 2> CharacterParam{};  // that parameter is the character
    bool operator==(const Judged&) const = default;
};

// None: the item does not read the player outside the subject and target, and is evaluated as it is.
[[nodiscard]] std::optional<Judged> For(const Item& acItem) noexcept;

// Speech checks read the player inside the engine's function, so they are worked out here for the character
// (docs/re/systems/dialogue.md, "Speech checks"): GetIntimidateSuccess and GetBribeSuccess.
inline constexpr uint16_t kGetBribeSuccess = 654;
inline constexpr uint16_t kGetIntimidateSuccess = 655;

// The character and the NPC it talks to, as those functions read them.
struct SpeechCheck
{
    float CharacterSpeech = 0.f;
    uint16_t CharacterLevel = 1;
    float NpcSpeech = 0.f;
    uint16_t NpcLevel = 1;
    float NpcMorality = 0.f;  // actor value 3
    uint8_t NpcConfidence = 0; // cowardly, cautious, average, brave, foolhardy
};

// Actor::WillIntimidateSucceed (37500): the strength before its perk entry (43), and what it must reach.
struct IntimidateSettings
{
    float SpeechcraftCurve = 0.f;               // fIntimidateSpeechcraftCurve
    std::array<float, 5> ConfidenceMults{};     // fIntimidateConfidenceMult{Cowardly ... Foolhardy}
};
[[nodiscard]] float IntimidateStrength(const SpeechCheck& acCheck, const IntimidateSettings& acSettings) noexcept;
[[nodiscard]] float IntimidateResistance(const SpeechCheck& acCheck, const IntimidateSettings& acSettings) noexcept;

// Actor::GetBribeAmount (37499), before its perk entry (46); the bribe succeeds with that much gold (37498).
struct BribeSettings
{
    float MoralityMult = 0.f;    // fBribeMoralityMult
    float NpcLevelMult = 0.f;    // fBribeNPCLevelMult
    float SpeechcraftMult = 0.f; // fBribeSpeechcraftMult
    float CostCurve = 0.f;       // fBribeCostCurve
    float Scale = 0.f;           // fBribeScale
};
[[nodiscard]] float BribeAmount(const SpeechCheck& acCheck, const BribeSettings& acSettings) noexcept;

// A condition's comparison (TESConditionItem flags' opCode: ==, !=, >, >=, <, <=) of its function's value.
[[nodiscard]] bool Compares(float aValue, uint8_t aOpCode, float aComparison) noexcept;
} // namespace JudgedRules
