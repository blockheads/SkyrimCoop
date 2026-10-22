#pragma once

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

// An actor in the session (its session id) or a reference from the game's data; both empty: none.
struct AiTarget
{
    bool operator==(const AiTarget& acRhs) const noexcept = default;

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
};
