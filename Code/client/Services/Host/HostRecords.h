#pragma once

#include <network/ENetInterface.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>
#include <Structs/RecordPart.h>

#include <map>
#include <unordered_map>

struct HostAuthority;
struct HostCharacters;
struct HostGame;
struct HostPlayers;
struct PlayerRecordRequest;

/**
 * @brief The guests' characters for the host's save (docs/design/join-flow.md, "As built"): each guest's record, kept
 * under the key it joined with; at each save, its place from the host's copy of its character.
 *
 * And every character's copy record (docs/design/one-simulator.md, piece 1): the parts its owner last declared, which
 * go to the other games and ride on each spawn of it.
 */
struct HostRecords
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rule with aAuthority.
    HostRecords(HostAuthority& aAuthority, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostRecords);

    // A guest's character: kept when it is under the key that guest joined with.
    void OnPlayerRecord(const PlayerRecordRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // The host's game is saving: each guest's record takes the place of the host's copy of its character.
    void OnSaving() const noexcept;
    // A guest leaves (before its characters go): its record keeps the place it left from.
    void OnPlayerLeft(ConnectionId_t aPlayer) const noexcept;

    // A character announced with these parts (they replace any held), and a character gone.
    void Announced(uint32_t aSessionId, const RecordParts& acParts) noexcept;
    void Forget(uint32_t aSessionId) noexcept { m_parts.erase(aSessionId); }
    // Every part held of a character, for a spawn of it.
    [[nodiscard]] RecordParts PartsOf(uint32_t aSessionId) const noexcept;

private:
    // A part from its actor's owner: kept if newer than the one held, and sent to every other game.
    void OnRecord(const RecordRequest& acRequest, ConnectionId_t aSender) noexcept;
    // A game asks for a part again: its actor's owner is told (R3).
    void OnRecordAsk(const RecordAskRequest& acRequest, ConnectionId_t aSender) const noexcept;
    void StorePlace(ConnectionId_t aPlayer) const noexcept;
    void OnPartyCall(const RoutedCallRequest& acRequest, ConnectionId_t aSender) noexcept;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
    // Each character's parts, by session id.
    std::unordered_map<uint32_t, std::map<RecordKind, RecordPart>> m_parts;
};
