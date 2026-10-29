#pragma once

#include <PartySize.h>
#include <network/ENetInterface.h>

#include <optional>
#include <NoCopyMove.h>

struct AuthenticationRequest;
struct HostAuthority;
struct HostGame;
struct HostPlayers;
struct HostCharacters;
struct HostSaveTransfer;

struct HostNetwork;

/**
 * @brief Admits matching games, shares the host's difficulty, and announces players joining, leaving and going down.
 */
struct HostSession
{
    using ConnectionId_t = Net::ConnectionId_t;

    static constexpr uint32_t cMaxPlayers = kMaxPartyPlayers;

    // Registers its rules with aAuthority.
    HostSession(HostAuthority& aAuthority, HostNetwork& aNetwork, HostPlayers& aPlayers, HostCharacters& aCharacters,
                HostSaveTransfer& aTransfer, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostSession);

    // The host's game set its difficulty: the others take it (those joining later get it in their answer).
    void OnDifficultyChange(uint32_t aDifficulty) const noexcept;

    // A game asks to join: accepted as a player, or refused and disconnected (the host's chat says why). One joining
    // from the main menu that passes gets the host's save instead (HostSaveTransfer), and asks again once loaded.
    void OnAuthentication(const AuthenticationRequest& acRequest, ConnectionId_t aSender) noexcept;
    // A player is leaving (still in the list): the others drop it from theirs.
    void OnPlayerLeft(ConnectionId_t aPlayer) const noexcept;

private:
    // The newcomer learns everyone already here, and they learn the newcomer.
    void Announce(ConnectionId_t aNewcomer) const noexcept;

    HostNetwork& m_network;
    HostPlayers& m_players;
    HostCharacters& m_characters;
    HostSaveTransfer& m_transfer;
    const HostGame& m_game;
};
