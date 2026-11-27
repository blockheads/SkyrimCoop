#pragma once

#include <Wire/Archive.h>

#include <Structs/GameId.h>
#include <array>
#include <vector>
#include <Wire/Buffer.h>

// An actor in the session (its session id) or a reference from the game's data; both empty: none.
struct AiTarget
{
    bool operator==(const AiTarget& acRhs) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(SessionId, RefId); }

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    uint32_t SessionId{};
    GameId RefId{};
};

// What an actor's AI is doing, as its owner's game has it; a copy holds its owner's (copy-contract.md, "AI state").
struct ActorAiState
{
    bool operator==(const ActorAiState& acRhs) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    // Actor::GetCurrentPackage, from the game's data; empty: none, or one the engine made.
    GameId Package{};
    // Actor::GetCurrentScene; empty: none.
    GameId Scene{};
    // Actor::GetCombatState: 0 none, 1 fighting, 2 searching; and its target (Actor::currentCombatTarget).
    uint8_t Combat{};
    AiTarget CombatTarget{};
    // HighProcessData::headTrackTarget: what it looks at for each reason (default, action, script, combat, dialogue,
    // procedure); the engine looks at the highest set one.
    std::array<AiTarget, 6> HeadTrack{};
    // The furniture it sits or sleeps in (Actor::GetOccupiedFurniture 38708); empty: none.
    AiTarget Furniture{};
    // Its combat group, unique across games (the owner's player session id << 32 | its group id; group ids start at
    // 0, session ids at 1), 0 none; and the
    // session actors among the group's targets (44706). Its copies hold the same in a real group.
    uint64_t CombatGroup{};
    std::vector<uint32_t> GroupTargets{};
};
