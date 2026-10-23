#pragma once

// Tier 2 (docs/design/testing.md): whole games in this process, each a World with its real services on a fake engine,
// the host's rules (HostAuthority) on the host's network. The host's game joins in-process as in Skyrim; guests join
// over enet on loopback, as from another machine.
#include <catch2/catch.hpp>

#include "FakeEngine.h"

#include <Components.h>
#include <RunningGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/HostService.h>
#include <Services/TransportService.h>
#include <World.h>

#include <chrono>
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
    // unless aJoin is false; each game loaded acPlugins first.
    explicit Party(size_t aGuests = 1, const WorldPlugins& acPlugins = {}, bool aJoin = true)
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
            if (aJoin)
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

    // The actor checksum: each actor a game owns in the session has one copy on every other game, matching it; a copy
    // a guest holds for a host NPC that went (HeldCopyComponent) is as the host's; no other actor is a copy.
    std::vector<std::string> ActorDifferences()
    {
        std::vector<std::string> differences;
        std::map<uint32_t, std::pair<World*, uint32_t>> owned; // session id: its owner's game and form id
        for (auto& pGame : m_games)
            for (const auto cEntity : pGame->view<LocalComponent, FormIdComponent>())
                owned[pGame->get<LocalComponent>(cEntity).Id] = {pGame.get(), pGame->get<FormIdComponent>(cEntity).Id};

        for (auto& pGame : m_games)
        {
            World& game = *pGame;
            const FakeEngine& cEngine = Engine(game);
            std::set<uint32_t> accounted; // the copies here that stand for an owner's actor
            for (const auto& [cSessionId, cOwner] : owned)
            {
                if (cOwner.first == &game)
                    continue;
                const FakeEngine& cOwnerEngine = Engine(*cOwner.first);
                const auto cWhat = std::format("{}'s copy of {}'s {:X} (session {:X})", cEngine.Name,
                                               cOwnerEngine.Name, cOwner.second, cSessionId);
                std::vector<uint32_t> copies;
                for (const auto cEntity : game.view<RemoteComponent>())
                    if (game.get<RemoteComponent>(cEntity).Id == cSessionId)
                        if (const auto* cpFormId = game.try_get<FormIdComponent>(cEntity))
                            copies.push_back(cpFormId->Id);
                if (copies.size() != 1)
                {
                    differences.push_back(std::format("{}: {} of them", cWhat, copies.size()));
                    continue;
                }
                accounted.insert(copies[0]);
                Compare(cWhat, cOwnerEngine, cOwner.second, cEngine, copies[0], differences);
            }
            for (const auto cEntity : game.view<HeldCopyComponent, FormIdComponent>())
            {
                const uint32_t cFormId = game.get<FormIdComponent>(cEntity).Id;
                accounted.insert(cFormId);
                if (&game != &Host())
                    Compare(std::format("{}'s held copy {:X}", cEngine.Name, cFormId), Engine(Host()), cFormId, cEngine,
                            cFormId, differences);
            }
            for (const uint32_t cCopy : cEngine.CopiesOf())
                if (!accounted.contains(cCopy))
                    differences.push_back(std::format("{}: {:X} is a copy of nothing anyone owns", cEngine.Name, cCopy));
        }
        return differences;
    }

    // Frames until the games agree on every actor (ActorDifferences), failing with the differences left.
    void Agree(const std::string& acStep)
    {
        Until([&] { return ActorDifferences().empty(); }, acStep, [&] {
            std::string why;
            for (const auto& cDifference : ActorDifferences())
                why += cDifference + "; ";
            return why;
        });
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

    // One frame of aGame alone, the others standing still (to order what each game does).
    void Step(World& aGame)
    {
        As(aGame, [&] { aGame.Update(); });
    }

    // Frames until aDone; fails naming acStep if it is not done within 2 s (enet's loopback takes real time), with
    // what acWhy reads then. With apOnly, only that game's frames.
    void Until(const std::function<bool()>& aDone, const std::string& acStep,
               const std::function<std::string()>& acWhy = {}, World* apOnly = nullptr)
    {
        const auto cDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        size_t frames = 0;
        while (!aDone())
        {
            if (std::chrono::steady_clock::now() > cDeadline)
                FAIL("never happened: " << acStep << (acWhy ? " (" + acWhy() + ")" : std::string()) << ", " << frames
                                        << " frames");
            apOnly ? Step(*apOnly) : Frame();
            ++frames;
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
    // How far a copy may stand from its owner's actor: FollowOwner leaves a copy within 64 units where it is.
    static constexpr float cNear = 64.f;

    // A copy against its owner's actor: there, in its cell and near it, dead or alive and enabled as it, flagged a copy.
    static void Compare(const std::string& acWhat, const FakeEngine& acOwner, uint32_t aOwnerId,
                        const FakeEngine& acCopies, uint32_t aCopyId, std::vector<std::string>& aDifferences)
    {
        const auto cOwnerIt = acOwner.Actors.find(aOwnerId);
        const auto cCopyIt = acCopies.Actors.find(aCopyId);
        if (cOwnerIt == acOwner.Actors.end() || cCopyIt == acCopies.Actors.end())
        {
            aDifferences.push_back(std::format("{}: {} has no actor {:X}", acWhat,
                                               cOwnerIt == acOwner.Actors.end() ? acOwner.Name : acCopies.Name,
                                               cOwnerIt == acOwner.Actors.end() ? aOwnerId : aCopyId));
            return;
        }
        const auto& cOwner = cOwnerIt->second;
        const auto& cCopy = cCopyIt->second;
        const auto cAt = [](const FakeEngine::Actor& acActor) {
            return glm::vec3(acActor.Position[0], acActor.Position[1], acActor.Position[2]);
        };
        if (!cCopy.Copy)
            aDifferences.push_back(std::format("{} ({:X}) is not flagged a copy", acWhat, aCopyId));
        // Outdoors the worldspace: by a cell's border the two can stand in neighbouring cells.
        if (cOwner.WorldSpace ? cCopy.WorldSpace != cOwner.WorldSpace : cCopy.Cell != cOwner.Cell)
            aDifferences.push_back(std::format("{} ({:X}) is in cell {:X} (worldspace {:X}), the owner's in {:X} ({:X})",
                                               acWhat, aCopyId, cCopy.Cell, cCopy.WorldSpace, cOwner.Cell,
                                               cOwner.WorldSpace));
        else if (const float cApart = glm::distance(cAt(cCopy), cAt(cOwner)); cApart > cNear)
            aDifferences.push_back(std::format("{} ({:X}) stands {:.0f} from the owner's", acWhat, aCopyId, cApart));
        if (cCopy.Dead != cOwner.Dead)
            aDifferences.push_back(std::format("{} ({:X}) is {}, the owner's {}", acWhat, aCopyId, cCopy.Dead ? "dead" : "alive",
                                               cOwner.Dead ? "dead" : "alive"));
        if (cCopy.Disabled != cOwner.Disabled)
            aDifferences.push_back(std::format("{} ({:X}) is {}, the owner's {}", acWhat, aCopyId,
                                               cCopy.Disabled ? "disabled" : "enabled", cOwner.Disabled ? "disabled" : "enabled"));
    }

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
