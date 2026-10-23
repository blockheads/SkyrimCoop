#pragma once

// A form's id on the wire (GameId). Every game in a session loads the same plugins in the same order (the join
// refuses any other, HostSession), so a form has the same id in all of them; only the forms a game creates are its own.
#include <Structs/GameId.h>

#include <cstdint>
#include <optional>

// A form this game created (load index 0xFF): no other game has it.
[[nodiscard]] inline bool IsCreatedForm(uint32_t aFormId) noexcept
{
    return aFormId >> 24 == 0xFF;
}

// A created form's id (ObjectChange::kCreatedModId): no game finds it by this id, the messages that carry one
// rebuild it from their data (world state's created references, enchantments, spawns).
[[nodiscard]] inline GameId CreatedId(uint32_t aFormId) noexcept
{
    return GameId(0xFFFFFFFF, aFormId & 0x00FFFFFF);
}

// This game's form id on the wire: none for a created form; 0 is GameId{}.
[[nodiscard]] inline std::optional<GameId> ToSession(uint32_t aFormId) noexcept
{
    if (IsCreatedForm(aFormId))
        return std::nullopt;
    return GameId(aFormId >> 24, aFormId & 0x00FFFFFF);
}

// ToSession, but a created form by its created id: for the messages that rebuild one.
[[nodiscard]] inline std::optional<GameId> ToSessionOrCreated(uint32_t aFormId) noexcept
{
    return IsCreatedForm(aFormId) ? CreatedId(aFormId) : ToSession(aFormId);
}

// The wire's id as this game's form id; 0 for a created form's id, which only its own game had.
[[nodiscard]] inline uint32_t ToGame(const GameId& acId) noexcept
{
    if (acId.ModId >= 0xFF || acId.BaseId > 0x00FFFFFF)
        return 0;
    return acId.ModId << 24 | acId.BaseId;
}
