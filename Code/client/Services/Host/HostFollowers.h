#pragma once

#include <Followers/State.h>
#include <cstdint>
#include <map>
#include <network/ENetInterface.h>
#include <vector>

struct HostAuthority;
struct HostNetwork;
struct FollowerTradeClosedRequest;
struct HostPlayers;
struct HostCharacters;
struct HostDeath;
struct HostGame;

// Shared vanilla slots, with a stable desired member and a temporary standing-party fallback.
struct HostFollowers
{
    HostFollowers(HostAuthority&, HostNetwork&, const HostPlayers&, const HostCharacters&, const HostDeath&,
                  const HostGame&) noexcept;
    void Update(uint64_t aTick) noexcept;

  private:
    void OnTradeClosed(const FollowerTradeClosedRequest&, Net::ConnectionId_t) noexcept;
    void GrantTrade(const Followers::State&, Net::ConnectionId_t) noexcept;
    HostNetwork& m_network;
    std::map<uint32_t, Net::ConnectionId_t> m_trading;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostDeath& m_death;
    const HostGame& m_game;
    struct Pending
    {
        Followers::Order Order;
        uint64_t Until;
    };
    std::vector<Pending> m_pending;
};
