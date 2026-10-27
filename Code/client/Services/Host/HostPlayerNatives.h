#pragma once

#include <NoCopyMove.h>
#include <Structs/ScriptValue.h>
#include <network/ENetInterface.h>

#include <unordered_map>
#include <Messages/RelayedMessages.h>

struct HostAuthority;
struct HostCharacters;
struct HostNetwork;
struct HostPlayers;

/**
 * @brief The script calls one game sends to others (docs/design/party-calls.md): a native on "the player" the host's
 * script called for a guest (Game.AdvanceSkill, Game.QueryStat) goes to that guest's game (docs/design/party-model.md,
 * rule 2), and one returning a value keeps the host's script waiting until that game answers, or leaves; a guest's own
 * script changing the world (a quest's stage) goes to the host's game, made there for that guest; the party's (the
 * screen, shows, progression) to every other game, named as the sender's.
 */
struct HostPlayerNatives
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostPlayerNatives(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers,
                      const HostCharacters& acCharacters) noexcept;

    NO_COPY_MOVE(HostPlayerNatives);

    // To a guest's character's owner: only from the host's game. To the host's game: only from a guest, for its
    // character. To the others: from any game, with its player id.
    void OnCall(const RoutedCallRequest& acRequest, ConnectionId_t aSender) noexcept;
    // A latent party call's original returned on the sender's game: to the others, with its player id.
    void OnCallEnded(const CallEndedRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // Only from the game it was sent to: back to the host's game.
    void OnPlayerNativeResult(const PlayerNativeResultRequest& acRequest, ConnectionId_t aSender) noexcept;
    // Before the player is off the list: what its game was asked goes unanswered (the host's script gets none).
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

  private:
    void ToOwner(const RoutedCallRequest& acRequest, ConnectionId_t aSender) noexcept;
    void ToHost(const RoutedCallRequest& acRequest, ConnectionId_t aSender) const noexcept;
    void ToOthers(const RoutedCallRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // The host's game: the script waiting on aReplyId gets what the guest's game returned (None: its default).
    void Answer(uint32_t aReplyId, const ScriptValue& acResult) const noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    std::unordered_map<uint32_t, ConnectionId_t> m_waiting; // reply id -> the game asked
};
