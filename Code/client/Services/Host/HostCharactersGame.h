#pragma once

#include <Messages/NotifyCharacterSpawn.h>
#include <Structs/GameId.h>
#include <Structs/PlayerRecord.h>

#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// What the host's rules read and change in its game: the host's actors, its copies and the guests' records. A part of HostGame.
struct HostCharactersGame
{
    // The host's own actors as its engine shows them now, but aExcludedSessionId.
    std::function<std::vector<NotifyCharacterSpawn>(uint32_t aExcludedSessionId)> OwnSpawns;
    // The host's copy of another player's character (position, items, hands, action) into the spawn, its values left
    // as its owner sent them; false when it has no copy.
    std::function<bool(uint32_t aSessionId, NotifyCharacterSpawn& aSpawn)> ReadCopy;
    // A spawn of the host's copy of an NPC another game owns now (a horse a guest rides), as its engine shows it.
    std::function<std::optional<NotifyCharacterSpawn>(uint32_t aSessionId)> CopySpawn;
    // One of the host's NPCs can take a pair's half: alive, its graph running (kProcessMe; kill-moves.md).
    std::function<bool(uint32_t aSessionId)> CanPair;
    // One of the host's NPCs its game could take back once claimed: no player's summon (TakeOwnership refuses one).
    std::function<bool(uint32_t aSessionId)> Claimable;
    // The guests' characters for the host's save (CharacterRecords): a guest's record, and its place at a save.
    std::function<void(const PlayerRecord& acRecord)> StoreRecord;
    std::function<void(const GameId& acSpell, bool aAdded)> StoreStorySpell;
    std::function<void(const std::string& acKey, const GameId& acWorldSpace, const GameId& acCell, const std::array<float, 3>& acPosition)> StorePlace;
    // A guest left: its record's timed state (effects, cooldowns) stops aging (CharacterRecords::Settle).
    std::function<void(const std::string& acKey)> SettleRecord;
};
