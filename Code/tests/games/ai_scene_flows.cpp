#include "Party.h"
#include "Fixtures.h"
#include <Events/AiStateChangeEvent.h>
#include <FormIds.h>

SCENARIO("A native scene clears after its end event and the gameplay copies follow", "[ai][scene-clear]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t merchant = 0;
    party.As(party.Host(), [&] { merchant = host.LoadNpc(0x13BA1, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(0x13BA1).size() == 1; }, "the guest has Belethor");
    const uint32_t cCopy = guest.CopiesOf(0x13BA1)[0];
    party.As(party.Host(), [&] {
        host.Actors.at(merchant).Ai.SceneId = 0xC791A;
        party.Host().GetDispatcher().trigger(AiStateChangeEvent{merchant});
    });
    party.Until([&] { return guest.GetActorAiFields(cCopy).SceneId == 0xC791A; },
                "the owner's active scene is the copy's gameplay answer");
    CHECK(guest.Actors.at(cCopy).LocalScene == 0);
    party.As(party.Host(), [&] { host.EndScene(merchant); });
    CHECK(host.GetActorAiFields(merchant).SceneId == 0);
    party.Until([&] { return guest.GetActorAiFields(cCopy).SceneId == 0; },
                "the copy's effective scene follows the owner after the native event");
    CHECK(guest.Actors.at(cCopy).LocalScene == 0);
    party.Agree("the merchants agree after the scene ends");
}

SCENARIO("A copy forgets the old package when its owner's package clears", "[ai][party-aggro][package-clear]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t npc = 0;
    party.As(party.Host(), [&] { npc = host.LoadNpc(0x13BA1, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(0x13BA1).size() == 1; }, "the guest has the actor");
    const auto cCopy = guest.CopiesOf(0x13BA1).front();
    constexpr uint32_t cPackage = 0x5C84B;
    party.As(party.Host(), [&] {
        host.Actors.at(npc).Ai.PackageId = cPackage;
        party.Host().GetDispatcher().trigger(AiStateChangeEvent{npc});
    });
    party.Until([&] { return guest.GetActorAiFields(cCopy).PackageId == cPackage; }, "the copy holds the owner's form package");
    party.As(party.Guest(), [&] { guest.Actors.at(cCopy).ExtraPackageFallback = cPackage; });
    party.As(party.Host(), [&] {
        host.Actors.at(npc).Ai.PackageId = 0;
        party.Host().GetDispatcher().trigger(AiStateChangeEvent{npc});
    });
    party.Until([&] { return guest.GetActorAiFields(cCopy).PackageId == 0; }, "the copy's gameplay reader no longer exposes the old fallback package");
}

SCENARIO("A copy looks at an actor whose session id came after its owner's look", "[ai][headtrack]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t speaker = 0;
    party.As(party.Host(), [&] { speaker = host.LoadNpc(0x13BA1, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(0x13BA1).size() == 1; }, "the guest has the speaker");
    // A created actor (no form the games share) appears and is looked at in one frame, before its session id.
    party.As(party.Host(), [&] {
        const uint32_t cListener = host.LoadNpc(0x1A67D, 0x31D2E, {});
        host.LooksAt(speaker, FakeEngine::kDialogueHeadTrack, cListener);
    });
    party.Until(
        [&] {
            const auto cListeners = guest.CopiesOf(0x1A67D);
            return cListeners.size() == 1 &&
                   guest.GetActorAiFields(guest.CopiesOf(0x13BA1)[0]).HeadTrackIds[FakeEngine::kDialogueHeadTrack] == cListeners[0];
        },
        "the speaker's copy looks at the listener's copy",
        [&] {
            return std::format("listener copies {}, slot {:X}", guest.CopiesOf(0x1A67D).size(),
                               guest.GetActorAiFields(guest.CopiesOf(0x13BA1)[0]).HeadTrackIds[FakeEngine::kDialogueHeadTrack]);
        });
    party.Agree("the games agree on every actor");
}

namespace
{
// Riverwood's sawmill (furniture-graphs.md): Hod's load idle enters a clip whose event, MillLogPileLoadStart, reaches
// the mill's graph through the link 39912 makes; the mill's graph then loads. On the guest his copy's same event
// reached nothing (duo 2026-11-22, copy-worker-drives-furniture).
constexpr uint32_t kHod = 0x1348A, kMill = 0x71DFC, kMillBase = 0x71C47, kHodMarker = 0x31D2E;

void MillGraphs(FakeEngine& aEngine)
{
    aEngine.GraphTransitions = {{{"Sandbox", "IdleMillLoadStart"}, "MillLoad"}, {{"MillLoad", "IdleStop"}, "Sandbox"}};
    aEngine.StateOutputs = {{"MillLoad", "MillLogPileLoadStart"}};
    aEngine.FurnitureTransitions = {{{"Idle", "MillLogPileLoadStart"}, "Loading"}};
}

ActionEvent MillLoad()
{
    ActionEvent load;
    load.ActionId = 0x13009;
    load.EventName = "IdleMillLoadStart";
    load.IdleId = 0x71C4A;
    return load;
}

std::string MillState(FakeEngine& aEngine, uint32_t aMill)
{
    const auto it = aEngine.FurnitureStates.find(aMill);
    return it == aEngine.FurnitureStates.end() ? "Idle" : it->second;
}
} // namespace

SCENARIO("A copy working a furniture drives the furniture's own graph as its owner does", "[ai][copies][furniture]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t hod = 0;
    party.As(party.Host(), [&] { hod = host.LoadNpc(kHod, kHodMarker, {}); });
    party.Until([&] { return guest.CopiesOf(kHod).size() == 1; }, "the guest has Hod's copy");
    const uint32_t cCopy = guest.CopiesOf(kHod)[0];
    MillGraphs(host);
    MillGraphs(guest);
    host.Actors.at(hod).GraphState = guest.Actors.at(cCopy).GraphState = "Sandbox";

    party.As(party.Host(), [&] { host.SitsIn(hod, kMill); });
    party.Until([&] { return guest.Actors.at(cCopy).LinkedFurniture == kMill; }, "Hod's copy is linked to the mill as Hod is",
                [&] { return std::format("copy linked to {:X}", guest.Actors.at(cCopy).LinkedFurniture); });
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(hod, MillLoad())); });
    party.Until([&] { return MillState(host, kMill) == "Loading" && MillState(guest, kMill) == "Loading"; },
                "the guest's mill loads from its worker's copy, as the host's does",
                [&] { return std::format("host's mill {}, guest's {}", MillState(host, kMill), MillState(guest, kMill)); });

    // Up again: the link goes with Hod's, and his next load reaches no mill on either game.
    party.As(party.Host(), [&] { host.SitsIn(hod, 0); });
    party.Until([&] { return guest.Actors.at(cCopy).LinkedFurniture == 0; }, "the standing copy is unlinked");
    const size_t cHostEvents = host.FurnitureGraphEvents[kMill].size(), cGuestEvents = guest.FurnitureGraphEvents[kMill].size();
    ActionEvent stop;
    stop.ActionId = 0x13009;
    stop.EventName = "IdleStop";
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(hod, stop)); });
    party.Until([&] { return guest.Actors.at(cCopy).GraphState == "Sandbox"; }, "the copy plays the stop");
    CHECK(host.FurnitureGraphEvents[kMill].size() == cHostEvents);
    CHECK(guest.FurnitureGraphEvents[kMill].size() == cGuestEvents);
}

SCENARIO("A copy sitting in a furniture whose 3D loads after links to it as the 3D loads", "[ai][copies][furniture]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t hod = 0;
    party.As(party.Host(), [&] { hod = host.LoadNpc(kHod, kHodMarker, {}); });
    party.Until([&] { return guest.CopiesOf(kHod).size() == 1; }, "the guest has Hod's copy");
    const uint32_t cCopy = guest.CopiesOf(kHod)[0];
    MillGraphs(host);
    MillGraphs(guest);
    host.Actors.at(hod).GraphState = guest.Actors.at(cCopy).GraphState = "Sandbox";
    guest.FurnitureUnloaded.insert(kMill); // the guest's mill still loading its 3D

    party.As(party.Host(), [&] { host.SitsIn(hod, kMill); });
    party.Until([&] { return guest.GetActorAiFields(cCopy).FurnitureId == kMill; }, "the copy holds Hod's seat");
    CHECK(guest.Actors.at(cCopy).LinkedFurniture == 0);
    party.As(party.Guest(), [&] { guest.LoadsFurniture(kMill); });
    party.Until([&] { return guest.Actors.at(cCopy).LinkedFurniture == kMill; }, "the copy links to the mill as its 3D loads");
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(hod, MillLoad())); });
    party.Until([&] { return MillState(guest, kMill) == "Loading"; }, "the guest's mill loads from its worker's copy",
                [&] { return std::format("guest's mill {}", MillState(guest, kMill)); });
}

SCENARIO("A copy sits in the guest's twin of a furniture the host's script placed", "[ai][copies][furniture][created]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t hod = 0, mill = 0;
    party.As(party.Host(), [&] { hod = host.LoadNpc(kHod, kHodMarker, {}); });
    party.Until([&] { return guest.CopiesOf(kHod).size() == 1; }, "the guest has Hod's copy");
    const uint32_t cCopy = guest.CopiesOf(kHod)[0];
    party.As(party.Host(), [&] { mill = host.ScriptPlaces(FakeEngine::kPlayer, kMillBase); });
    const auto cTwin = [&]() -> uint32_t {
        for (const auto& [cId, cReference] : guest.References)
            if (cReference.Base == kMillBase)
                return cId;
        return 0;
    };
    party.Until([&] { return cTwin() != 0; }, "the guest's game makes its twin of the placed mill");
    REQUIRE(IsCreatedForm(mill));
    party.As(party.Host(), [&] { host.SitsIn(hod, mill); });
    party.Until([&] { return guest.Actors.at(cCopy).LinkedFurniture == cTwin(); }, "Hod's copy is linked to the guest's twin",
                [&] { return std::format("host's mill {:X}, guest's twin {:X}, copy linked to {:X}", mill, cTwin(),
                                         guest.Actors.at(cCopy).LinkedFurniture); });
}
