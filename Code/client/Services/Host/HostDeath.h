#pragma once

#include <Messages/DeathMessages.h>
#include <Messages/RelayedMessages.h>
#include <Structs/ActorAiState.h>
#include <network/ENetInterface.h>

#include <map>
#include <optional>
#include <set>
#include <string>

struct HostAuthority;
struct HostNetwork;
struct HostPlayers;
struct HostCharacters;
struct HostGame;

/**
 * @brief The party going down and coming back (docs/design/death.md), the host's rules:
 *
 *   down         a player's game says its character went down (PlayerDeathRequest, one generation per fall)
 *   recovery     once kRecoverySeconds pass with no standing player fighting (its own combat, or a host NPC's on it),
 *                every downed player respawns beside the standing player with the lowest session id
 *   wipe         every player down: the host's game loads its previous save (HostGame::PartyWipe) and the party
 *                rolls back together
 *
 * Every player gets the party's state (NotifyPartyDeath) on each change.
 */
struct HostDeath
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Out of combat this long, the downed players respawn.
    static constexpr double kRecoverySeconds = 10.0;
    // Each one respawns this far from the standing player, a sixth of a turn round from the last.
    static constexpr float kRespawnDistance = 100.f;
    static constexpr float kRespawnTurn = 1.04719755f; // 60 degrees

    HostDeath(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostCharacters& acCharacters,
              const HostGame& acGame) noexcept;

    // Every frame: the game time it took (seconds), so the recovery timer stands still while the game is paused.
    void Update(double aDelta) noexcept;
    // A player's character is in the world (it counts from here), and the AI state a character arrived with.
    void Entered(ConnectionId_t aPlayer, uint32_t aCharacterId) noexcept;
    void InitialAi(uint32_t aCharacterId, const ActorAiState& acState) noexcept;
    void Left(ConnectionId_t aPlayer) noexcept;
    // The wipe's reload failed: every player is told why.
    void FailReload(const std::string& acError) noexcept;
    [[nodiscard]] bool Standing(ConnectionId_t aPlayer) const noexcept
    {
        const auto it = m_party.find(aPlayer);
        return it != m_party.end() && it->second.Known && !it->second.Body.Down;
    }
    [[nodiscard]] bool Wiped() const noexcept { return m_wiped; }

private:
    void OnDeath(const PlayerDeathRequest& acRequest, ConnectionId_t aSender) noexcept;
    // A character's combat, from its owner (the relayed AiStateRequest, watched).
    void OnAiState(const AiStateRequest& acRequest, ConnectionId_t aSender) noexcept;
    // Who is down and who fights decides a wipe, the recovery timer, or neither.
    void Reconsider() noexcept;
    [[nodiscard]] bool StandingPlayerFights(const std::set<uint32_t>& acStanding) const noexcept;
    void Respawn() noexcept;
    void Broadcast() const noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;

    struct Player
    {
        PartyDeathPlayer Body;  // its character, down or not, in which fall
        bool Known{};           // its game said its state (a player who just joined holds the timer until it does)
        bool Respawning{};      // told to respawn for this fall
    };
    std::map<ConnectionId_t, Player> m_party;
    std::map<uint32_t, ActorAiState> m_ai;                 // each character's last combat state, by session id
    std::map<uint32_t, std::set<uint32_t>> m_targets;      // each fighting character's targets since it started
    double m_now{};
    std::optional<double> m_recoveryAt;
    bool m_combat{};
    bool m_wiped{};
    std::string m_error;
};
