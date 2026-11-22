#pragma once

#include <Games/Skyrim/DetectionRules.h>
#include <network/ENetInterface.h>

#include <NoCopyMove.h>

#include <unordered_map>

struct HostCharacters;
struct HostGame;
struct HostPlayers;

/**
 * @brief A guest's stealth eye is what the host's NPCs see of its player (one-simulator.md piece 8, stealth.md "The eye
 * is the host's"): each frame the host's game reads its NPCs' detection of each guest's copy, as its HUD asks of its
 * own player, and sends that guest the reading when it changes.
 */
struct HostEye
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostEye(const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostEye);

    void Update() noexcept;
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept { m_sent.erase(aPlayer); }

private:
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
    // Each guest's last reading sent.
    std::unordered_map<ConnectionId_t, DetectionRules::EyeReading> m_sent;
};
