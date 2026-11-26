#pragma once

#include <Messages/CrimeMessages.h>
#include <network/ENetInterface.h>
#include <unordered_map>

struct HostRouter;
struct HostNetwork;
struct HostPlayers;
struct HostCharacters;
struct HostCrimeGame;

struct HostCrime
{
    using ConnectionId_t = Net::ConnectionId_t;
    HostCrime(HostRouter&, HostNetwork&, const HostPlayers&, const HostCharacters&, const HostCrimeGame&) noexcept;
    void Update() noexcept;
    void Entered(ConnectionId_t) noexcept;
    void Left(ConnectionId_t aPlayer) noexcept
    {
        m_sequences.erase(aPlayer);
        m_caughtLocks.erase(aPlayer);
    }

  private:
    void OnRequest(const CrimeRequest&, ConnectionId_t) noexcept;
    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostCrimeGame& m_game;
    std::unordered_map<ConnectionId_t, uint32_t> m_sequences;
    std::unordered_map<ConnectionId_t, uint32_t> m_caughtLocks;
    std::vector<CrimeGold> m_gold;
    uint32_t m_confiscationId{};
};
