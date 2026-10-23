#pragma once

// Tier 2 (docs/design/testing.md): whole games in this process, each a World with its real services on a fake engine,
// the host's rules (HostAuthority) on the host's network. The host's game joins in-process as in Skyrim; guests join
// over enet on loopback, as from another machine.
#include <catch2/catch.hpp>

#include "FakeEngine.h"

#include <RunningGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/HostService.h>
#include <Services/TransportService.h>
#include <World.h>

#include <chrono>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace GameTest
{
// The plugins' world, the same in every game: what each game's engine holds before anyone joins.
using WorldPlugins = std::function<void(FakeEngine&)>;

struct Party
{
    // The host's game ("Dovahkiin") hosting (HostService) and aGuests guests ("Lydia", "Aela", "Serana"), all
    // joined; each game loaded acPlugins first.
    explicit Party(size_t aGuests = 1, const WorldPlugins& acPlugins = {})
    {
        m_games.push_back(NewGame("Dovahkiin", acPlugins));
        As(Host(), [&] {
            REQUIRE(Host().GetHostService().StartHosting(cPort));
            Host().GetTransport().ConnectLocal(*Host().GetHostService().GetNetwork());
        });
        Until([&] { return Host().GetTransport().IsOnline(); }, "the host's game joins its own session");

        const char* cNames[] = {"Lydia", "Aela", "Serana"};
        for (size_t i = 0; i < aGuests; ++i)
        {
            m_games.push_back(NewGame(cNames[i], acPlugins));
            Engine(*m_games.back()).IsGuest = true;
            Join(*m_games.back());
        }
    }

    // aGuest joins (again).
    void Join(World& aGuest)
    {
        const uint16_t cPort = Host().GetHostService().GetNetwork()->GetPort();
        As(aGuest, [&] { REQUIRE(aGuest.GetTransport().Connect(std::format("127.0.0.1:{}", cPort))); });
        Until([&] { return aGuest.GetTransport().IsOnline(); }, std::format("{} joins", Engine(aGuest).Name));
    }

    // The checksum: each reference in a cell the host and a guest both have loaded, where the two differ.
    std::vector<std::string> Differences()
    {
        std::vector<std::string> differences;
        const FakeEngine& cHost = Engine(Host());
        for (size_t i = 1; i < m_games.size(); ++i)
        {
            const FakeEngine& cGuest = Engine(*m_games[i]);
            for (const auto& [cId, cReference] : cHost.References)
                if (cHost.Attached.contains(cReference.Cell) && cGuest.Attached.contains(cReference.Cell) &&
                    cGuest.References.at(cId) != cReference)
                    differences.push_back(std::format("{}: reference {:X} differs from the host's", cGuest.Name, cId));
        }
        return differences;
    }

    ~Party()
    {
        for (auto& pGame : m_games)
            As(*pGame, [&] { pGame->GetTransport().Close(); });
        SetCurrentWorld(nullptr);
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

    // aAction on aGame: World::Get() is that game while it runs.
    void As(World& aGame, const std::function<void()>& aAction)
    {
        SetCurrentWorld(&aGame);
        aAction();
    }

    // One frame of every game (the host's runs its network and rules, HostService::OnUpdate).
    void Frame()
    {
        for (auto& pGame : m_games)
            As(*pGame, [&] { pGame->Update(); });
    }

    // Frames until aDone; fails naming acStep if it is not done within 2 s (enet's loopback takes real time), with
    // what acWhy reads then.
    void Until(const std::function<bool()>& aDone, const std::string& acStep, const std::function<std::string()>& acWhy = {})
    {
        const auto cDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!aDone())
        {
            if (std::chrono::steady_clock::now() > cDeadline)
                FAIL("never happened: " << acStep << (acWhy ? " (" + acWhy() + ")" : std::string()));
            Frame();
        }
    }

    // A script on aGame, no player's action behind it (a quest's stage fragment).
    void WorldScript(World& aGame, const LocalCall& acCall)
    {
        As(aGame, [&] { Engine(aGame).RunScript(acCall); });
    }

    // aGuest leaves the session.
    void Leave(World& aGuest)
    {
        As(aGuest, [&] { aGuest.GetTransport().Close(); });
    }

  private:
    static constexpr uint16_t cPort = 21578;

    std::unique_ptr<World> NewGame(const char* acpName, const WorldPlugins& acPlugins)
    {
        auto pGame = std::make_unique<World>();
        Engine(*pGame).Name = acpName;
        if (acPlugins)
            acPlugins(Engine(*pGame));
        return pGame;
    }

    std::vector<std::unique_ptr<World>> m_games;
};
} // namespace GameTest

using namespace GameTest;
