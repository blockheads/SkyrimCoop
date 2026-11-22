#include "Party.h"
#include "Fixtures.h"
#include <Events/AiStateChangeEvent.h>

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

SCENARIO("A copy working a furniture drives the furniture's own graph as its owner does", "[ai][copies][furniture]")
{
    // Riverwood's sawmill: Hod's load idle reaches the mill's graph through the link 39912 makes (furniture-graphs.md);
    // on the guest his copy sent the same event to nothing (duo 2026-11-22, copy-worker-drives-furniture).
    constexpr uint32_t cHod = 0x1348A, cMill = 0x71DFC, cMarker = 0x31D2E;
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t hod = 0;
    party.As(party.Host(), [&] { hod = host.LoadNpc(cHod, cMarker, {}); });
    party.Until([&] { return guest.CopiesOf(cHod).size() == 1; }, "the guest has Hod's copy");
    const uint32_t cCopy = guest.CopiesOf(cHod)[0];
    for (auto* engine : {&host, &guest})
        engine->GraphTransitions = {{{"Sandbox", "IdleMillLoadStart"}, "MillLoad"}, {{"MillLoad", "IdleStop"}, "Sandbox"}};
    host.Actors.at(hod).GraphState = guest.Actors.at(cCopy).GraphState = "Sandbox";

    party.As(party.Host(), [&] { host.SitsIn(hod, cMill); });
    party.Until([&] { return guest.Actors.at(cCopy).LinkedFurniture == cMill; }, "Hod's copy is linked to the mill as Hod is",
                [&] { return std::format("copy linked to {:X}", guest.Actors.at(cCopy).LinkedFurniture); });
    ActionEvent load;
    load.ActionId = 0x13009;
    load.EventName = "IdleMillLoadStart";
    load.IdleId = 0x71C4A;
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(hod, load)); });
    party.Until([&] { return guest.FurnitureGraphEvents[cMill] == host.FurnitureGraphEvents[cMill]; },
                "the guest's mill gets the load its worker's copy plays, as the host's does", [&] {
                    const auto cList = [](const std::vector<std::string>& acEvents) {
                        std::string text;
                        for (const auto& cEvent : acEvents)
                            text += cEvent + " ";
                        return text;
                    };
                    return std::format("host's mill: {}; guest's: {}", cList(host.FurnitureGraphEvents[cMill]),
                                       cList(guest.FurnitureGraphEvents[cMill]));
                });
    CHECK(host.FurnitureGraphEvents[cMill] == std::vector<std::string>{"IdleMillLoadStart"});

    // Up again: the link goes with Hod's, and his next idle reaches no mill on either game.
    party.As(party.Host(), [&] { host.SitsIn(hod, 0); });
    party.Until([&] { return guest.Actors.at(cCopy).LinkedFurniture == 0; }, "the standing copy is unlinked");
    ActionEvent stop;
    stop.ActionId = 0x13009;
    stop.EventName = "IdleStop";
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(hod, stop)); });
    party.Until([&] { return guest.Actors.at(cCopy).GraphState == "Sandbox"; }, "the copy plays the stop");
    CHECK(host.FurnitureGraphEvents[cMill].size() == 1);
    CHECK(guest.FurnitureGraphEvents[cMill].size() == 1);
}
