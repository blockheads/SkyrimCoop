// The shared specs (Code/tests/specs/*.json) against the fake engine: the same files run in Skyrim
// (docs/design/testing.md, "Shared specs"). One section per spec: `GameTests "[spec]" -c <name>` runs one.
#include "Party.h"

#include <Structs/GridCellCoords.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace
{
using Json = nlohmann::json;

std::vector<std::filesystem::path> Specs()
{
    std::vector<std::filesystem::path> specs;
    for (const auto& cEntry : std::filesystem::directory_iterator(COOP_SPECS_DIR))
        if (cEntry.path().extension() == ".json")
            specs.push_back(cEntry.path());
    std::ranges::sort(specs);
    return specs;
}

LocalValue ValueOf(const Json& acValue)
{
    if (acValue.is_boolean())
        return acValue.get<bool>();
    if (acValue.is_number_integer())
        return acValue.get<int32_t>();
    if (acValue.is_number_float())
        return acValue.get<float>();
    if (acValue.is_string())
        return acValue.get<std::string>();
    return std::monostate{};
}

// The scenarios' cell (QASmoke) as each game's plugins hold it here: the references the specs name. The ids of
// fixed references are Skyrim's; a role ("inner-door") is what coop_test.py looks up in the cell there.
constexpr uint32_t kQaSmoke = 0x00031D2E;
constexpr uint32_t kInnerDoor = 0x0001686E; // a stand-in id: coop_test.py's inner_door() finds the real one
constexpr uint32_t kSmallChest = 0x000C2CE8;
constexpr uint32_t kVilkas = 0x0001A695;
constexpr uint32_t kVilkasHome = 0x00016EF6; // a stand-in for where the game placed him (Jorrvaskr)
constexpr glm::vec3 kStage{0.f, 200.f, 0.f}; // a stand-in for coop_test.py's stage_spot()
constexpr uint32_t kTamriel = 0x0000003C;
constexpr glm::vec3 kElsewhere{4096.f * 5 + 100.f, 0.f, 0.f}; // "elsewhere": Riverwood in Skyrim, an exterior

void QaSmoke(FakeEngine& aEngine)
{
    aEngine.References[kInnerDoor] = {.Cell = kQaSmoke};
    aEngine.References[kSmallChest] = {.Cell = kQaSmoke, .Items = {{0x0000000A, 3}, {0x0003AD5B, 1}}}; // lockpicks, a potion
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kQaSmoke;
    aEngine.LoadCell(kQaSmoke);
    aEngine.PlaceNpc(kVilkas, 0x0001A694, kVilkasHome, {300.f, 300.f, 0.f});
    aEngine.Exterior(kTamriel, 0x00020000, 6);
}

uint32_t Id(const Json& acValue)
{
    const std::string cText = acValue.get<std::string>();
    if (cText == "inner-door")
        return kInnerDoor;
    return static_cast<uint32_t>(std::stoul(cText, nullptr, 16));
}

// Runs one spec's steps on a party, each named in a failure ("step 3: ...").
struct SpecRun
{
    Party& Games;
    std::string Name;
    std::map<std::string, uint32_t> Npcs; // the host's NPCs by the name a step gave them ("as")

    // An actor a step names: a name it was given, or a hex id; the host's reference.
    uint32_t HostActor(const Json& acValue, const std::string& acWhat)
    {
        const std::string cText = acValue.get<std::string>();
        if (const auto it = Npcs.find(cText); it != Npcs.end())
            return it->second;
        if (cText.starts_with("0x"))
            return static_cast<uint32_t>(std::stoul(cText, nullptr, 16));
        FAIL(acWhat << ": no actor \"" << cText << "\"");
        return 0;
    }

    // The host's actor as aGame has it: itself on the host; on a guest its copy (by session id), or the guest's own
    // reference held as a copy (0: none).
    uint32_t ActorOn(World& aGame, uint32_t aHostActor)
    {
        if (&aGame == &Games.Host())
            return aHostActor;
        World& host = Games.Host();
        for (const auto cEntity : host.view<FormIdComponent, LocalComponent>())
            if (host.get<FormIdComponent>(cEntity).Id == aHostActor)
                for (const auto cCopy : aGame.view<FormIdComponent, RemoteComponent>())
                    if (aGame.get<RemoteComponent>(cCopy).Id == host.get<LocalComponent>(cEntity).Id)
                        return aGame.get<FormIdComponent>(cCopy).Id;
        for (const auto cEntity : aGame.view<FormIdComponent, HeldCopyComponent>())
            if (aGame.get<FormIdComponent>(cEntity).Id == aHostActor)
                return aHostActor;
        return 0;
    }

    // The host's game owns the actor and every guest has a copy of it.
    bool Shown(uint32_t aHostActor)
    {
        for (size_t i = 0; i + 1 < Games.GameCount(); ++i)
            if (!ActorOn(Games.Guest(i), aHostActor) || !Party::Engine(Games.Guest(i)).IsActorCopy(ActorOn(Games.Guest(i), aHostActor)))
                return false;
        for (const auto cEntity : Games.Host().view<FormIdComponent, LocalComponent>())
            if (Games.Host().get<FormIdComponent>(cEntity).Id == aHostActor)
                return true;
        return false;
    }

    // Every game's player is where the host's is: its interior, or outdoors its worldspace within the loaded grid.
    bool Together()
    {
        const auto& cHost = Party::Engine(Games.Host()).Actors.at(FakeEngine::kPlayer);
        for (size_t i = 0; i + 1 < Games.GameCount(); ++i)
        {
            const auto& cGuest = Party::Engine(Games.Guest(i)).Actors.at(FakeEngine::kPlayer);
            const auto cAt = [](const FakeEngine::Actor& acActor) {
                return GridCellCoords::CalculateGridCellCoords(acActor.Position[0], acActor.Position[1]);
            };
            if (cHost.WorldSpace ? cGuest.WorldSpace != cHost.WorldSpace || !GridCellCoords::IsCellInGridCell(cAt(cGuest), cAt(cHost), false)
                                 : cGuest.Cell != cHost.Cell)
                return false;
        }
        return true;
    }

    World& Game(const std::string& acName)
    {
        if (acName == "host")
            return Games.Host();
        if (acName == "guest")
            return Games.Guest(0);
        if (acName == "guest2")
            return Games.Guest(1);
        FAIL(Name << ": no game \"" << acName << "\"");
        return Games.Host();
    }

    void Run(const Json& acSteps, bool aCleanup)
    {
        for (size_t i = 0; i < acSteps.size(); ++i)
            Step(acSteps[i],
                 std::format("{}, {} {}: {}", Name, aCleanup ? "cleanup" : "step", i + 1, acSteps[i].dump()));
    }

    void Step(const Json& acStep, const std::string& acWhat)
    {
        INFO(acWhat);
        if (acStep.contains("do"))
            return Do(acStep, acWhat);
        const std::string cExpect = acStep.value("expect", "");
        World& game = Game(acStep.value("game", "host"));
        const FakeEngine& cEngine = Party::Engine(game);
        std::function<bool()> holds;
        if (cExpect == "controls" && acStep.contains("equals"))
        {
            const auto cMask = static_cast<uint32_t>(std::stoul(acStep["equals"].get<std::string>(), nullptr, 16));
            holds = [&cEngine, cMask] { return cEngine.Controls == cMask; };
        }
        else if (cExpect == "controls" && acStep.contains("taken"))
        {
            const bool cTaken = acStep["taken"].get<bool>();
            holds = [&cEngine, cTaken] { return (cEngine.Controls != FakeEngine::kAllControls) == cTaken; };
        }
        else if (cExpect == "shake")
        {
            const bool cShaking = acStep["shaking"].get<bool>();
            holds = [&cEngine, cShaking] { return (cEngine.ShakeSeconds > 0) == cShaking; };
        }
        else if (cExpect == "lock")
        {
            const uint32_t cRef = Id(acStep["ref"]);
            const bool cLocked = acStep["locked"].get<bool>();
            const std::optional<int> cLevel = acStep.contains("level") ? std::optional(acStep["level"].get<int>()) : std::nullopt;
            holds = [&cEngine, cRef, cLocked, cLevel] {
                const auto& cReference = cEngine.References.at(cRef);
                return cReference.Locked == cLocked && (!cLevel || cReference.LockLevel == *cLevel);
            };
        }
        else if (cExpect == "actors agree")
        {
            if (acStep.value("now", false))
            {
                const auto cDifferences = Games.ActorDifferences();
                for (const auto& cDifference : cDifferences)
                    UNSCOPED_INFO(cDifference);
                CHECK(cDifferences.empty());
            }
            else
                Games.Agree(acWhat);
            return;
        }
        else if (cExpect == "actor")
        {
            const uint32_t cHostActor = HostActor(acStep["actor"], acWhat);
            World* pGame = &game;
            holds = [this, pGame, cHostActor, acStep] {
                const uint32_t cId = ActorOn(*pGame, cHostActor);
                const FakeEngine& cHere = Party::Engine(*pGame);
                return cId && (!acStep.contains("dead") || cHere.IsActorDead(cId) == acStep["dead"].get<bool>()) &&
                       (!acStep.contains("disabled") || cHere.IsActorDisabled(cId) == acStep["disabled"].get<bool>()) &&
                       (!acStep.contains("copy") || cHere.IsActorCopy(cId) == acStep["copy"].get<bool>());
            };
        }
        else if (cExpect == "together")
            holds = [this] { return Together(); };
        else if (cExpect == "down")
        {
            // "within" is Skyrim's wait (a respawn takes seconds there); the fake's frames are quicker than its 2 s.
            const bool cDown = acStep["down"].get<bool>();
            holds = [&cEngine, cDown] { return !cEngine.IsPlayerAlive() == cDown; };
        }
        else if (cExpect == "items")
        {
            const uint32_t cRef = Id(acStep["ref"]);
            const uint32_t cItem = Id(acStep["item"]);
            const int32_t cCount = acStep["count"].get<int32_t>();
            holds = [&cEngine, cRef, cItem, cCount] {
                const auto& cItems = cEngine.References.at(cRef).Items;
                const auto it = cItems.find(cItem);
                return (it != cItems.end() ? it->second : 0) == cCount;
            };
        }
        else
            FAIL(acWhat << ": no such expectation");
        if (acStep.value("now", false))
        {
            INFO(std::format("controls {:X}, shake {:.2f}", cEngine.Controls, cEngine.ShakeSeconds));
            CHECK(holds());
        }
        else
            Games.Until(holds, acWhat);
    }

    void Do(const Json& acStep, const std::string& acWhat)
    {
        const std::string cDo = acStep["do"].get<std::string>();
        if (cDo == "script")
        {
            World& game = Game(acStep.value("game", "host"));
            const std::string cCall = acStep["call"].get<std::string>();
            const auto cDot = cCall.find('.');
            LocalCall call{.Class = cCall.substr(0, cDot), .Function = cCall.substr(cDot + 1)};
            for (const auto& cArgument : acStep.value("args", Json::array()))
                call.Arguments.push_back(ValueOf(cArgument));
            const uint32_t cCause = acStep.value("by", "world") == "player" ? 0x14 : 0;
            Games.As(game, [&] { Party::Engine(game).RunScript(call, cCause); });
        }
        else if (cDo == "leave")
        {
            World& game = Game(acStep["game"].get<std::string>());
            if (game.GetTransport().IsOnline())
            {
                Games.Leave(game);
                Games.Until([&] { return !game.GetTransport().IsOnline(); }, acWhat);
            }
        }
        else if (cDo == "join")
        {
            World& game = Game(acStep["game"].get<std::string>());
            if (!game.GetTransport().IsOnline())
                Games.Join(game);
        }
        else if (cDo == "lock")
        {
            // The player locks or picks it (the harness's `lock`).
            World& game = Game(acStep.value("game", "host"));
            Games.As(game, [&] {
                Party::Engine(game).PlayerSetsLock(Id(acStep["ref"]), acStep["locked"].get<bool>(), acStep.value("level", 0));
            });
        }
        else if (cDo == "add_items" || cDo == "remove_items")
        {
            // The console's additem / removeitem on a container: the host's world takes it, a guest's proposes it.
            World& game = Game(acStep.value("game", "host"));
            FakeEngine& engine = Party::Engine(game);
            const uint32_t cRef = Id(acStep["ref"]);
            const uint32_t cItem = Id(acStep["item"]);
            const int32_t cCount = acStep["count"].get<int32_t>() * (cDo == "add_items" ? 1 : -1);
            Games.As(game, [&] {
                const int32_t cNow = std::max(0, engine.References.at(cRef).Items[cItem] + cCount);
                engine.PlayerSetsItems(cRef, cItem, cNow);
            });
        }
        else if (cDo == "spawn_npc")
        {
            // The host's player places one (the console's placeatme), next to it.
            FakeEngine& host = Party::Engine(Games.Host());
            uint32_t npc = 0;
            Games.As(Games.Host(), [&] {
                const auto& cPlayer = host.Actors.at(FakeEngine::kPlayer);
                npc = host.LoadNpc(Id(acStep["base"]), cPlayer.Cell,
                                   {cPlayer.Position[0] + 60.f, cPlayer.Position[1], cPlayer.Position[2]});
            });
            Npcs[acStep["as"].get<std::string>()] = npc;
            Games.Until([&] { return Shown(npc); }, acWhat + " (the host's game owns it, every guest has a copy)");
        }
        else if (cDo == "stage_npc")
        {
            // One of the host's placed NPCs brought to the stage (coop_test.py's stage_host_npc).
            const uint32_t cNpc = Id(acStep["ref"]);
            Npcs[acStep["as"].get<std::string>()] = cNpc;
            Games.As(Games.Host(), [&] { Party::Engine(Games.Host()).MoveActorTo(cNpc, kQaSmoke, kStage.x, kStage.y, kStage.z); });
            Games.Until([&] { return Shown(cNpc); }, acWhat + " (the host's game owns it, every guest has a copy)");
        }
        else if (cDo == "send_home")
        {
            // Back where the game placed it (move_actor's home).
            const uint32_t cNpc = HostActor(acStep["actor"], acWhat);
            Games.As(Games.Host(), [&] { Party::Engine(Games.Host()).MoveActorTo(cNpc, kVilkasHome, 300.f, 300.f, 0.f); });
        }
        else if (cDo == "kill" || cDo == "disable" || cDo == "enable")
        {
            // The host's console on its actor.
            const uint32_t cNpc = HostActor(acStep["actor"], acWhat);
            FakeEngine& host = Party::Engine(Games.Host());
            Games.As(Games.Host(), [&] {
                if (cDo == "kill")
                    host.KillActor(cNpc);
                else if (cDo == "disable")
                    host.DisableActor(cNpc, false);
                else
                    host.EnableActor(cNpc);
            });
        }
        else if (cDo == "deadly_hit")
        {
            World& game = Game(acStep.value("game", "host"));
            Games.As(game, [&] { Party::Engine(game).Hit(FakeEngine::kPlayer, 10000.f); });
        }
        else if (cDo == "go")
        {
            World& game = Game(acStep["game"].get<std::string>());
            if (acStep["cell"].get<std::string>() != "elsewhere")
                FAIL(acWhat << ": no such place");
            FakeEngine& engine = Party::Engine(game);
            Games.As(game, [&] { engine.PlayerTravels(engine.CellAt(kTamriel, kElsewhere), kElsewhere); });
        }
        else if (cDo == "marker")
        {
            // A party shake after the calls before it: once the other game shakes, they have all run there.
            World& from = Game(acStep["from"].get<std::string>());
            const FakeEngine& cTo = Party::Engine(Game(acStep["to"].get<std::string>()));
            Games.Until([&] { return cTo.ShakeSeconds <= 0; }, acWhat + " (no shake before the marker)");
            const LocalCall cShake{
                .Class = "Game", .Function = "ShakeCamera", .Arguments = {std::monostate{}, 0.5f, 2.0f}};
            Games.As(from, [&] { Party::Engine(from).RunScript(cShake); });
            Games.Until([&] { return cTo.ShakeSeconds > 0; }, acWhat + " (the marker's shake arrives)");
        }
        else
            FAIL(acWhat << ": no such step");
    }
};
} // namespace

TEST_CASE("Shared specs against the fake engine", "[spec]")
{
    const auto cSpecs = Specs();
    REQUIRE(!cSpecs.empty());
    for (const auto& cPath : cSpecs)
    {
        std::ifstream file(cPath);
        const Json cSpec = Json::parse(file);
        const std::string cName = cSpec.at("name").get<std::string>();
        DYNAMIC_SECTION(cName)
        {
            Party party(cSpec.value("guests", 1), QaSmoke);
            SpecRun run{party, cName};
            run.Run(cSpec.at("steps"), false);
            run.Run(cSpec.value("cleanup", Json::array()), true);
        }
    }
}
