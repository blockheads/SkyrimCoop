#pragma once

// Tier 2 (docs/design/testing.md): whole games in this process, each a World with its real services on a fake engine,
// the host's rules (HostAuthority) on the host's network. The host's game joins in-process as in Skyrim; guests join
// over enet on loopback, as from another machine.
#include <catch2/catch.hpp>

#include "FakeEngine.h"
#include "FakeSteam.h"
#include "UdpRelay.h"

#include <Components.h>
#include <RunningGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/HostService.h>
#include <Services/TransportService.h>
#include <World.h>

#include <chrono>
#include <cstdlib>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace GameTest
{
// The plugins' world, the same in every game: what each game's engine holds before anyone joins.
using WorldPlugins = std::function<void(FakeEngine&)>;

struct Party
{
    // The host's game ("Dovahkiin") hosting (HostService) and aGuests guests ("Lydia", "Aela", "Serana"), joined
    // unless aJoin is false; each game loaded acPlugins first. With acNet: the guests' line, else COOP_NET's.
    explicit Party(size_t aGuests = 1, const WorldPlugins& acPlugins = {}, bool aJoin = true,
                   const std::optional<NetConditions>& acNet = {});

    // aGuest joins (again): straight to the host's port, or under COOP_NET through its relay.
    void Join(World& aGuest);

    // The checksum: each reference in a cell the host and a guest both have loaded, where the two differ.
    std::vector<std::string> Differences();

    // The actor checksum: each actor a game owns in the session has one copy on every other game, matching it; a copy
    // a guest holds for a host NPC that went (HeldCopyComponent) is as the host's; no other actor is a copy.
    std::vector<std::string> ActorDifferences();

    // Frames until the games agree on every actor (ActorDifferences), failing with the differences left.
    void Agree(const std::string& acStep);

    ~Party();

    size_t GameCount() const
    {
        return m_games.size();
    }
    World& Host()
    {
        return *m_games[0];
    }
    World& Guest(size_t aIndex = 0)
    {
        return *m_games[1 + aIndex];
    }
    static FakeEngine& Engine(World& aGame)
    {
        return aGame.ctx().at<FakeEngine&>();
    }

    static FakeSteam& Steam(World& aGame)
    {
        return aGame.ctx().at<FakeSteam&>();
    }

    // aAction on aGame: World::Get() is that game while it runs.
    void As(World& aGame, const std::function<void()>& aAction);

    // One frame of every game (the host's runs its network and rules, HostService::OnUpdate).
    void Frame();

    // One frame of aGame alone, the others standing still (to order what each game does).
    void Step(World& aGame);

    // Frames until aDone; fails naming acStep if it is not done within 2 s (enet's loopback takes real time; 20 s
    // under COOP_NET, for its resends), or aTimeout if longer, with acWhy read then. With apOnly, only that game's frames.
    void Until(const std::function<bool()>& aDone, const std::string& acStep,
               const std::function<std::string()>& acWhy = {}, World* apOnly = nullptr,
               std::chrono::seconds aTimeout = std::chrono::seconds{0});

    // Under COOP_NET: the guests' datagrams go through relays (UdpRelay).
    [[nodiscard]] bool Conditioned() const noexcept
    {
        return m_net.Any();
    }

    // A script on aGame, no player's action behind it (a quest's stage fragment).
    void WorldScript(World& aGame, const LocalCall& acCall);

    // aGuest leaves the session.
    void Leave(World& aGuest);

  private:
    void PumpRelays();

    // The host's first port; scripts/check.sh gives each shard its own (GAMETESTS_PORT).
    static uint16_t Port();
    NetConditions m_net = NetConditions::FromEnvironment();
    std::map<World*, std::unique_ptr<UdpRelay>> m_relays;
    std::unique_ptr<World> NewGame(const char* acpName, const WorldPlugins& acPlugins);

    std::vector<std::unique_ptr<World>> m_games;
};
} // namespace GameTest

using namespace GameTest;
