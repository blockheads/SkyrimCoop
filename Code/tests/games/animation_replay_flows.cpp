#include "Party.h"
#include "Fixtures.h"
#include <Services/CharacterService.h>
#include <Messages/NotifyReferencesMove.h>
#include <Systems/InterpolationSystem.h>

TEST_CASE("An NPC movement replay does not restore the owner's finished sheathe transition", "[copies][animation][replay-weapon]")
{
    const bool cRecover = GENERATE(false, true);
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t elk = 0;
    party.As(party.Host(), [&] { elk = host.LoadNpc(0x23AB2, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(0x23AB2).size() == 1; }, "the elk's copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x23AB2).front();
    if (cRecover)
    {
        host.GraphTransitions[{"Moving", "moveStart"}] = "Moving";
        host.Actors.at(elk).GraphState = "Moving";
        guest.GraphTransitions = host.GraphTransitions;
        guest.Actors.at(cCopy).GraphState = "OtherIdle";
    }
    ActionEvent move;
    move.ActionId = 0x959F8;
    move.IdleId = 0xA75C7;
    move.EventName = "moveStart";
    party.As(party.Host(), [&] {
        host.Actors.at(elk).Weapon = WeaponState::Sheathing;
        REQUIRE(host.PerformAnimation(elk, move));
        host.Actors.at(elk).Weapon = WeaponState::Sheathed;
    });
    party.Until([&] { return guest.GetActorLatestAnimation(cCopy).IdleId == move.IdleId; }, "the old movement action reaches the copy");
    CHECK(guest.GetWeaponState(cCopy) == WeaponState::Sheathed);
    party.Agree("the elk keeps its current hands state after the movement replay");
}

TEST_CASE("A dialogue idle query never advances a copy before the owner performs it", "[animation][dialogue][replay-query]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    for (auto* engine : {&host, &guest})
        engine->GraphTransitions = {{{"MotionDrivenDialogue", "MotionDrivenDialogueGeneric"}, "DialogueGeneric"}};
    uint32_t lydia = 0;
    party.As(party.Host(), [&] {
        lydia = host.LoadNpc(0xA2C8E, 0x31D2E, {});
        host.Actors.at(lydia).GraphState = "MotionDrivenDialogue";
    });
    party.Until([&] { return guest.CopiesOf(0xA2C8E).size() == 1; }, "Lydia's copy appears");
    const uint32_t cCopy = guest.CopiesOf(0xA2C8E).front();
    party.Until([&] { return guest.Actors.at(cCopy).GraphState == "MotionDrivenDialogue"; },
                "the copy loads its owner's starting graph");
    ActionEvent query;
    query.ActionId = 0x489F1;
    query.EventName = "MotionDrivenDialogueGeneric";
    query.IdleId = 0x10DEB6;
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(lydia, query, true)); });
    CHECK(host.Actors.at(lydia).GraphState == "MotionDrivenDialogue");
    const auto cQueryTick = party.Host().GetTick();
    party.Until([&] { return party.Host().GetTick() >= cQueryTick + 350; },
                "the query has time to cross the network and copy playback clock");
    CHECK(guest.Actors.at(cCopy).GraphState == "MotionDrivenDialogue");
    CHECK(guest.GetActorLatestAnimation(cCopy).IdleId == 0);

    // The actual pick can differ from the queried idle (Lydia: 10DEB6 vs 10DEB8).
    query.IdleId = 0x10DEB8;
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(lydia, query)); });
    party.Until([&] { return guest.GetActorLatestAnimation(cCopy).IdleId == query.IdleId; },
                "the copy performs the owner's actual dialogue gesture");
    CHECK(guest.RefusedAnimations[cCopy] == 0);
    CHECK(guest.Actors.at(cCopy).GraphState == host.Actors.at(lydia).GraphState);
}

TEST_CASE("A refused replay recovers the owner's graph after a local clip advanced", "[animation][dialogue][replay-graph]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    for (auto* engine : {&host, &guest})
        engine->GraphTransitions = {{{"DialogueGeneric", "IdleStopInstant"}, "MotionDrivenDialogue"}};
    uint32_t lydia = 0;
    party.As(party.Host(), [&] {
        lydia = host.LoadNpc(0xA2C8E, 0x31D2E, {});
        host.Actors.at(lydia).GraphState = "DialogueGeneric";
    });
    party.Until([&] { return guest.CopiesOf(0xA2C8E).size() == 1; }, "Lydia's copy appears");
    const uint32_t cCopy = guest.CopiesOf(0xA2C8E).front();
    party.Until([&] { return guest.Actors.at(cCopy).GraphState == "DialogueGeneric"; }, "the spawn graph loads");
    // Native clips advance autonomously: the copy has already left the state's IdleStop transition.
    guest.Actors.at(cCopy).GraphState = "MotionDrivenDialogue";
    ActionEvent stop;
    stop.ActionId = 0x7F8E3;
    stop.EventName = "IdleStopInstant";
    stop.IdleId = 0x7F8E4;
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(lydia, stop)); });
    party.Until([&] { return guest.GetActorLatestAnimation(cCopy).IdleId == stop.IdleId; },
                "the refused replay converges from its owner's resulting graph");
    CHECK(guest.Actors.at(cCopy).GraphState == host.Actors.at(lydia).GraphState);
    CHECK(guest.RefusedAnimations[cCopy] > 0);
}

TEST_CASE("NPC actions wait for the same playback time as their movement", "[animation][replay-time]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t lydia = 0;
    party.As(party.Host(), [&] { lydia = host.LoadNpc(0xA2C8E, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(0xA2C8E).size() == 1; }, "Lydia's copy appears");
    const auto cCopy = guest.CopiesOf(0xA2C8E).front();
    uint32_t sid = 0;
    for (auto&& [entity, form, local] : party.Host().view<FormIdComponent, LocalComponent>().each())
        if (form.Id == lydia) sid = local.Id;
    REQUIRE(sid != 0);
    NotifyReferencesMove snapshot;
    snapshot.Tick = party.Guest().GetTransport().GetTick() + 250;
    ActionEvent gesture;
    gesture.Tick = snapshot.Tick;
    gesture.ActionId = 0x489F1;
    gesture.IdleId = 0xF779D;
    gesture.EventName = "MotionDrivenIdle";
    snapshot.Updates[sid].ActionEvents.push_back(gesture);
    party.As(party.Guest(), [&] { party.Guest().GetCharacterService().OnReferencesMoveRequest(snapshot); });
    const auto cBefore = guest.GetActorLatestAnimation(cCopy);
    party.Step(party.Guest());
    CHECK(guest.GetActorLatestAnimation(cCopy) == cBefore);
    party.Until([&] { return guest.GetActorLatestAnimation(cCopy).IdleId == gesture.IdleId; },
                "the copy performs the gesture when playback reaches it");
}

TEST_CASE("A burst of NPC actions advances through graph updates in order", "[animation][replay-staging]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t lydia = 0;
    party.As(party.Host(), [&] {
        lydia = host.LoadNpc(0xA2C8E, 0x31D2E, {});
        host.Actors.at(lydia).GraphState = "MotionDrivenDialogue";
    });
    party.Until([&] { return guest.CopiesOf(0xA2C8E).size() == 1; }, "Lydia's copy appears");
    const auto cCopy = guest.CopiesOf(0xA2C8E).front();
    entt::entity entity = entt::null;
    for (auto&& [id, form, remote] : party.Guest().view<FormIdComponent, RemoteComponent>().each())
        if (form.Id == cCopy) entity = id;
    REQUIRE((entity != entt::null));
    auto& queue = party.Guest().get<RemoteAnimationComponent>(entity).TimePoints;
    ActionEvent first;
    first.Tick = 1;
    first.ActionId = 0x489F1;
    first.IdleId = 0xF779D;
    first.EventName = "MotionDrivenIdle";
    auto second = first;
    second.IdleId = 0x10DEB8;
    second.EventName = "MotionDrivenDialogueGeneric";
    queue.push_back(first);
    queue.push_back(second);
    const auto cBefore = guest.GetActorLatestAnimation(cCopy);
    party.Step(party.Guest());
    CHECK(guest.GetActorLatestAnimation(cCopy) == cBefore);
    CHECK(queue.size() == 2);
    party.Step(party.Guest());
    party.Step(party.Guest());
    CHECK(guest.GetActorLatestAnimation(cCopy).IdleId == first.IdleId);
    CHECK(queue.size() == 1);
    party.Until([&] { return guest.GetActorLatestAnimation(cCopy).IdleId == second.IdleId; }, "the next gesture follows the first");
}

TEST_CASE("Attached NPC copies retain actions until their behavior graph is ready", "[animation][replay-ready]")
{
    const bool cGraphReady = GENERATE(false, true);
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t lydia = 0;
    party.As(party.Host(), [&] { lydia = host.LoadNpc(0xA2C8E, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(0xA2C8E).size() == 1; }, "Lydia's copy appears");
    const auto cCopy = guest.CopiesOf(0xA2C8E).front();
    party.Agree("the copy starts in sync");
    guest.Actors.at(cCopy).GraphReady = cGraphReady;
    guest.Actors.at(cCopy).GraphUpdating = !cGraphReady;
    REQUIRE(guest.ActorHas3D(cCopy));
    ActionEvent gesture;
    gesture.ActionId = 0x489F1;
    gesture.IdleId = 0x10DEB8;
    gesture.EventName = "MotionDrivenDialogueGeneric";
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(lydia, gesture)); });
    party.Until([&] {
        for (auto&& [entity, form, animation] : party.Guest().view<FormIdComponent, RemoteAnimationComponent>().each())
            if (form.Id == cCopy)
                return std::ranges::any_of(animation.TimePoints, [&](const ActionEvent& action) {
                    return action.IdleId == gesture.IdleId && InterpolationSystem::PlaybackTick(party.Guest(), entity) >= action.Tick;
                });
        return false;
    }, "the queued gesture is due for playback");
    if (!cGraphReady)
    {
        for (size_t i = 0; i < 100; ++i) party.Frame();
        CHECK(guest.GetActorLatestAnimation(cCopy).IdleId == 0);
        const auto differences = party.ActorDifferences();
        CHECK(std::ranges::any_of(differences, [](const std::string& difference) { return difference.contains("animation"); }));
        guest.Actors.at(cCopy).GraphReady = true;
    }
    // A ready graph without animation jobs still takes the native ten-update main-thread fallback.
    party.Agree("the queued gesture eventually reaches the ready graph even without graph jobs");
    CHECK(guest.GetActorLatestAnimation(cCopy).IdleId == gesture.IdleId);
}
