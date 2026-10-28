#pragma once

#include <network/ENetInterface.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

struct HostAuthority;
struct HostCharacters;
struct HostGame;
struct HostPlayers;
struct PlayerRecordRequest;

/**
 * @brief The guests' characters for the host's save (docs/design/join-flow.md, "As built"): each guest's record, kept
 * under the key it joined with; at each save, its place from the host's copy of its character.
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

private:
    void OnPartyCall(const RoutedCallRequest& acRequest, ConnectionId_t aSender) noexcept;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
};
