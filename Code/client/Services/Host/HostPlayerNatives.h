#pragma once

#include <NoCopyMove.h>
#include <Structs/ScriptValue.h>
#include <network/ENetInterface.h>

#include <unordered_map>

struct HostAuthority;
struct HostCharacters;
struct HostNetwork;
struct HostPlayers;
struct PlayerNativeRequest;
struct PlayerNativeResultRequest;

/**
 * @brief Natives on "the player" the host's script called for a guest (Game.AdvanceSkill, Game.QueryStat): that
 * guest's game runs them (docs/design/party-model.md, rule 2). One returning a value keeps the host's script waiting
 * until that game answers, or leaves.
 */
struct HostPlayerNatives
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostPlayerNatives(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers,
                      const HostCharacters& acCharacters) noexcept;

    NO_COPY_MOVE(HostPlayerNatives);

    // Only from the host's game: the native goes to the owner of the character it ran for.
    void OnPlayerNative(const PlayerNativeRequest& acRequest, ConnectionId_t aSender) noexcept;
    // Only from the game it was sent to: back to the host's game.
    void OnPlayerNativeResult(const PlayerNativeResultRequest& acRequest, ConnectionId_t aSender) noexcept;
    // Before the player is off the list: what its game was asked goes unanswered (the host's script gets none).
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

  private:
    // The host's game: the script waiting on aReplyId gets what the guest's game returned (None: its default).
    void Answer(uint32_t aReplyId, const ScriptValue& acResult) const noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    std::unordered_map<uint32_t, ConnectionId_t> m_waiting; // reply id -> the game asked
};
