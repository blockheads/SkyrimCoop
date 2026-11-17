#pragma once

#include "FakeEngine.h"

#include <Bridges/IDevToolsBridge.h>

#include <sstream>
#include <vector>

// The dev commands' console lines, kept; the few the flows read run on the fake engine as Skyrim's console runs them.
struct FakeDevTools final : IDevToolsBridge
{
    explicit FakeDevTools(FakeEngine& aEngine) noexcept : Engine(aEngine)
    {
    }

    void RunConsole(const std::string& acLine) noexcept override
    {
        Lines.push_back(acLine);
        std::istringstream words(acLine);
        std::string command, argument;
        uint32_t count = 1;
        words >> command >> argument;
        if (command.starts_with("player.restoreav"))
        {
            const uint32_t cIndex = argument == "health"    ? FakeEngine::kHealth
                                    : argument == "magicka" ? FakeEngine::kMagicka
                                                            : FakeEngine::kStamina;
            auto& values = Engine.Actors.at(FakeEngine::kPlayer).Values;
            values.ActorValuesList[cIndex] = values.ActorMaxValuesList[cIndex];
        }
        // <reference>.placeatme <base> <count>: new NPCs at the reference.
        else if (const auto cDot = command.find(".placeatme"); cDot != std::string::npos)
        {
            words >> count;
            const std::string cAt = command.substr(0, cDot);
            const uint32_t cRef =
                cAt == "player" ? FakeEngine::kPlayer : static_cast<uint32_t>(std::stoul(cAt, nullptr, 16));
            const auto& cActor = Engine.Actors.at(cRef);
            for (uint32_t i = 0; i < count; ++i)
                Engine.LoadNpc(
                    static_cast<uint32_t>(std::stoul(argument, nullptr, 16)), cActor.Cell,
                    {cActor.Position[0] + 100.f * static_cast<float>(i + 1), cActor.Position[1], cActor.Position[2]});
        }
    }

    // The fake knows no factions: every living NPC this game owns is an enemy.
    uint32_t KillHostiles() noexcept override
    {
        std::vector<uint32_t> killed;
        for (const auto& [cId, cActor] : Engine.Actors)
            if (!cActor.Player && !cActor.Copy && !cActor.Dead)
                killed.push_back(cId);
        for (const uint32_t cId : killed)
            Engine.KillActor(cId);
        return static_cast<uint32_t>(killed.size());
    }

    bool IsGodMode() noexcept override
    {
        return God;
    }
    void SetGodMode(bool aOn) noexcept override
    {
        God = aOn;
    }
    void SetKillMoveOdds(std::optional<uint8_t> aOdds) noexcept override
    {
        KillMoveOdds = aOdds;
    }

    FakeEngine& Engine;
    std::vector<std::string> Lines;
    std::optional<uint8_t> KillMoveOdds;
    bool God = false; // the engine's god mode byte (40501)
};
