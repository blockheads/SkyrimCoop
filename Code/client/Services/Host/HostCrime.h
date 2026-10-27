#pragma once

#include <Messages/CrimeMessages.h>
#include <network/ENetInterface.h>
#include <unordered_map>

struct HostAuthority;
struct HostNetwork;
struct HostPlayers;
struct HostCharacters;
struct HostGame;

struct HostCrime
{
    using ConnectionId_t = Net::ConnectionId_t;
    HostCrime(HostAuthority&, HostNetwork&, const HostPlayers&, const HostCharacters&, const HostGame&) noexcept;
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
    const HostGame& m_game;
    std::unordered_map<ConnectionId_t, uint32_t> m_sequences;
    std::unordered_map<ConnectionId_t, uint32_t> m_caughtLocks;
    std::vector<CrimeGold> m_gold;
    uint32_t m_confiscationId{};
};
