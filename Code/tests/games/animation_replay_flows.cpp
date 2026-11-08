#include "Party.h"
#include "Fixtures.h"
#include <Services/CharacterService.h>
#include <Messages/NotifyReferencesMove.h>
#include <Systems/InterpolationSystem.h>
#include <Components/InterpolationComponent.h>

namespace
{
} // namespace

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

TEST_CASE("A copy plays an idle's first clip, not what follows it", "[animation][dialogue][replay-clips]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    // Skyrim (Lucan): the owner's MT_IdleHDLeftEnter plays 5 s; its copy's reads t=0.01333 at_end, then HDLeftIdle.
    for (auto* engine : {&host, &guest})
    {
        engine->GraphTransitions = {{{"Standing", "IdleHDLeftAngry"}, "IdleHDLeftEnter"}};
        engine->SkippedClips = {{"IdleHDLeftEnter", "IdleHDLeftIdle"}};
        engine->ClipFiles = {{"IdleHDLeftAngry", "IdleHDLeftEnter"}};
    }
    uint32_t lucan = 0;
    party.As(party.Host(), [&] {
        lucan = host.LoadNpc(0x13487, 0x31D2E, {});
        host.Actors.at(lucan).GraphState = "Standing";
    });
    party.Until([&] { return guest.CopiesOf(0x13487).size() == 1; }, "Lucan's copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x13487).front();
    party.Until([&] { return guest.Actors.at(cCopy).GraphState == "Standing"; }, "the spawn graph loads");

    ActionEvent idle;
    idle.ActionId = 0x13002;
    idle.EventName = "IdleHDLeftAngry";
    idle.IdleId = 0xF64D7;
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(lucan, idle)); });
    CHECK(host.Actors.at(lucan).GraphState == "IdleHDLeftEnter");
    party.Until([&] { return guest.GetActorLatestAnimation(cCopy).IdleId == idle.IdleId; }, "the copy takes the idle");
    CHECK(guest.Actors.at(cCopy).LoadedClips.contains("IdleHDLeftAngry"));
    CHECK(guest.Actors.at(cCopy).GraphState == "IdleHDLeftEnter");
}

TEST_CASE("A copy's graph makes its owner's random picks", "[animation][dialogue][replay-picks]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    // Skyrim (Lucan): one action, ExpressiveE on the host and ExpressiveH on the guest; each graph draws its own.
    for (auto* engine : {&host, &guest})
    {
        engine->GraphTransitions = {{{"Standing", "Gesture"}, "Expressive"}};
        engine->GraphPicks = {{"Expressive", {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N"}}};
    }
    uint32_t lucan = 0;
    party.As(party.Host(), [&] {
        lucan = host.LoadNpc(0x13487, 0x31D2E, {});
        host.Actors.at(lucan).GraphState = "Standing";
        host.Actors.at(lucan).GraphRandom = 0x7DE6C53E;
    });
    party.Until([&] { return guest.CopiesOf(0x13487).size() == 1; }, "Lucan's copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x13487).front();
    party.Until([&] { return guest.Actors.at(cCopy).GraphState == "Standing"; }, "the spawn graph loads");
    guest.Actors.at(cCopy).GraphRandom = 0xC34A5C6E;

    ActionEvent gesture;
    gesture.ActionId = 0x13002;
    gesture.EventName = "Gesture";
    gesture.IdleId = 0xF64D7;
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(lucan, gesture)); });
    party.Until([&] { return guest.GetActorLatestAnimation(cCopy).IdleId == gesture.IdleId; }, "the copy takes the gesture");
    CHECK(guest.Actors.at(cCopy).GraphState == host.Actors.at(lucan).GraphState);
}

TEST_CASE("A copy's graph loops as its owner's with no action to follow", "[animation][replay-picks]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    // Skyrim (Lucan's counter): the host drew 3 loops and the guest 1, their clips 0.45 s apart: arms cross 19 s apart.
    for (auto* engine : {&host, &guest})
        engine->Loop = FakeEngine::GraphLoop{"CounterIdle", "CounterArms", 4000, 3};
    uint32_t lucan = 0;
    party.As(party.Host(), [&] {
        lucan = host.LoadNpc(0x13487, 0x31D2E, {});
        host.Actors.at(lucan).GraphState = "CounterIdle";
        host.Actors.at(lucan).GraphRandom = 0x7DE6C53E;
    });
    party.Until([&] { return guest.CopiesOf(0x13487).size() == 1; }, "Lucan's copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x13487).front();
    party.Until([&] { return guest.Actors.at(cCopy).GraphState == "CounterIdle"; }, "the spawn graph loads");
    guest.Actors.at(cCopy).GraphRandom = 0xC34A5C6E;

    const auto& cOwner = host.Actors.at(lucan);
    const auto& cCopied = guest.Actors.at(cCopy);
    party.Until([&] { return cCopied.LoopDraws.size() >= 6; }, "the copy's graph draws six times", {}, nullptr, std::chrono::seconds{60});
    // A copy's first draw can be its own, made before its owner's first sync point. From then on they are one sequence.
    const std::vector<uint32_t> cLast(cCopied.LoopDraws.end() - 3, cCopied.LoopDraws.end());
    CHECK(std::ranges::search(cOwner.LoopDraws, cLast).begin() != cOwner.LoopDraws.end());
}

TEST_CASE("A copy arriving mid-loop falls in with its owner at the next pick", "[animation][replay-points]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    // Skyrim (Lucan's counter, a second visit): the copy came up a loop ahead, in the other clip, and stayed there.
    for (auto* engine : {&host, &guest})
        engine->Loop = FakeEngine::GraphLoop{"CounterIdle", "CounterArms", 4000, 2};
    uint32_t lucan = 0;
    party.As(party.Host(), [&] {
        lucan = host.LoadNpc(0x13487, 0x31D2E, {});
        host.Actors.at(lucan).GraphState = "CounterIdle";
        host.Actors.at(lucan).GraphRandom = 0x7DE6C53E;
    });
    party.Until([&] { return guest.CopiesOf(0x13487).size() == 1; }, "Lucan's copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x13487).front();
    party.Until([&] { return guest.Actors.at(cCopy).GraphState == "CounterIdle"; }, "the spawn graph loads");
    auto& copied = guest.Actors.at(cCopy);
    copied.GraphState = "CounterArms";
    copied.GraphRandom = 0xC34A5C6E;
    copied.LoopFrames = 700;

    const auto& cOwner = host.Actors.at(lucan);
    const size_t cDraws = cOwner.LoopDraws.size();
    party.Until([&] { return cOwner.LoopDraws.size() >= cDraws + 2; }, "the owner picks twice", {}, nullptr, std::chrono::seconds{60});
    // From here its picks and its clip are its owner's, a moment behind.
    const size_t cFrom = copied.LoopDraws.size();
    const size_t cOwnerFrom = cOwner.LoopDraws.size();
    uint32_t apart = 0, frames = 0;
    party.Until([&] {
        apart += copied.GraphState != cOwner.GraphState;
        ++frames;
        return cOwner.LoopDraws.size() >= cOwnerFrom + 4;
    }, "the owner picks four more times", {}, nullptr, std::chrono::seconds{60});
    REQUIRE(copied.LoopDraws.size() >= cFrom + 3);
    const std::vector<uint32_t> cLast(copied.LoopDraws.end() - 3, copied.LoopDraws.end());
    CHECK(std::ranges::search(cOwner.LoopDraws, cLast).begin() != cOwner.LoopDraws.end());
    // Frames against a delay in real time: a loaded machine reads more apart. Out of step reads 38% and up.
    CHECK(apart * 4 < frames);
}

TEST_CASE("A sync point waits for the actions before it", "[animation][replay-points]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    for (auto* engine : {&host, &guest})
        engine->GraphTransitions = {{{"Standing", "Gesture"}, "Gestured"}, {{"Later", "Gesture"}, "Gestured"}};
    uint32_t lucan = 0;
    party.As(party.Host(), [&] {
        lucan = host.LoadNpc(0x13487, 0x31D2E, {});
        host.Actors.at(lucan).GraphState = "Standing";
    });
    party.Until([&] { return guest.CopiesOf(0x13487).size() == 1; }, "Lucan's copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x13487).front();
    party.Until([&] { return guest.Actors.at(cCopy).GraphState == "Standing"; }, "the spawn graph loads");
    uint32_t sid = 0;
    for (auto&& [entity, form, local] : party.Host().view<FormIdComponent, LocalComponent>().each())
        if (form.Id == lucan) sid = local.Id;
    REQUIRE(sid != 0);

    // Both overdue when the copy looks: its older action plays first, or it undoes what the point loaded.
    auto& copied = guest.Actors.at(cCopy);
    copied.GraphState = "Later";
    const std::string cLater = guest.SaveActorGraphState(cCopy);
    copied.GraphState = "Standing";
    NotifyReferencesMove snapshot;
    snapshot.Tick = party.Guest().GetTransport().GetTick() - 2000;
    ActionEvent gesture;
    gesture.Tick = snapshot.Tick;
    gesture.ActionId = 0x13002;
    gesture.EventName = "Gesture";
    gesture.IdleId = 0xF64D7;
    ActionEvent point;
    point.Tick = snapshot.Tick + 500;
    point.Variables.GraphRandom = 0x7DE6C53E;
    point.GraphState = cLater;
    snapshot.Updates[sid].ActionEvents = {gesture, point};
    party.As(party.Guest(), [&] { party.Guest().GetCharacterService().OnReferencesMoveRequest(snapshot); });
    party.Until([&] { return guest.GetActorLatestAnimation(cCopy).IdleId == gesture.IdleId; }, "the copy takes the older action");
    party.Until([&] { return copied.GraphState == "Later"; }, "the sync point lands after it");
    CHECK(copied.GraphRandom == point.Variables.GraphRandom);
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

namespace
{
// The wolf's canned turn (Skyrim: wolf 9B0DD, Riverwood, 2026-11-07). TurnCannedR180 ends with no action; DefaultBehavior
// leaves CannedTurnState and StandingBehavior starts from iSyncTurnState. Just before, the owner's AI performed TurnStop,
// which its graph refused but which set iSyncTurnState 1 (39004: StandingIdle). Its copy never got that refused action,
// read its snapshot's 0 (LoopingTurnRight) and turned on the spot for 4 to 20 s, or stayed in CannedTurnState when
// iSyncIdleLocomotion, its exit's condition, was still another value.
struct WolfTurn
{
    Party party;
    FakeEngine& host;
    FakeEngine& guest;
    uint32_t wolf = 0;
    uint32_t copy = 0;

    // aExitCondition: CannedTurnState's exit needs iSyncIdleLocomotion 0 or 1, which the owner's refused MoveStop sets.
    explicit WolfTurn(const std::optional<NetConditions>& acNet = {}, bool aExitCondition = false)
        : party(1, SmokeWorld, true, acNet)
        , host(Party::Engine(party.Host()))
        , guest(Party::Engine(party.Guest()))
    {
        for (auto* engine : {&host, &guest})
        {
            engine->GraphTransitions = {{{"Standing", "cannedTurnRight180"}, "CannedTurn"}, {{"StandingIdle", "cannedTurnRight180"}, "CannedTurn"}};
            engine->Bound = FakeEngine::BoundStart{"CannedTurn", 1000, 0, {"LoopingTurnRight", "StandingIdle"}, {"turnStop"}};
            engine->ActionWrites = {{"turnStop", {0, 1}}, {"moveStop", {1, 0}}};
            if (aExitCondition)
            {
                engine->Bound->OwnerEnds.push_back("moveStop");
                engine->Bound->ExitVariable = 1;
                engine->Bound->ExitValues = {0, 1};
            }
        }
        party.As(party.Host(), [&] {
            wolf = host.LoadNpc(0x23AB2, 0x31D2E, {});
            host.Actors.at(wolf).GraphState = "Standing";
            host.Actors.at(wolf).GraphVariables.Integers = {0, 2};
        });
        party.Until([&] { return guest.CopiesOf(0x23AB2).size() == 1; }, "the wolf's copy appears");
        copy = guest.CopiesOf(0x23AB2).front();
        party.Until([&] { return guest.Actors.at(copy).GraphState == "Standing"; }, "the spawn graph loads");
        party.Until([&] { return guest.Actors.at(copy).GraphVariables.Integers.size() > 1 && guest.Actors.at(copy).GraphVariables.Integers[1] == 2; },
                    "the copy has its owner's variables from before the turn");
    }

    // aWhileTurning: once the copy took the turn, before its clip ends. aOwnerEnded: as the owner's clip ends.
    void Turn(const std::function<void()>& aWhileTurning = {}, const std::function<void()>& aOwnerEnded = {})
    {
        ActionEvent turn;
        turn.ActionId = 0x13002;
        turn.EventName = "cannedTurnRight180";
        const uint64_t cBefore = guest.GetActorLatestAnimation(copy).Tick;
        party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(wolf, turn)); });
        party.Until([&] { return host.Actors.at(wolf).GraphState == "StandingIdle"; }, "the owner's canned turn ends standing", {},
                    &party.Host());
        if (aOwnerEnded)
            aOwnerEnded();
        party.Until([&] { return guest.GetActorLatestAnimation(copy).EventName == turn.EventName && guest.GetActorLatestAnimation(copy).Tick != cBefore; },
                    "the copy takes the turn");
        if (aWhileTurning)
            aWhileTurning();
        party.Until([&] { return guest.Actors.at(copy).GraphState != "CannedTurn"; }, "the copy's canned turn ends");
    }
};
} // namespace

TEST_CASE("A copy's machine started at a clip's end reads its owner's new variable", "[animation][replay-start]")
{
    // StandingBehavior starts from the iSyncTurnState the copy has as its own clip ends. Its owner's refused turnStop
    // set 1 just before its clip ended: sent as variables only, the copy loads it at the same point (ReplayStep.h).
    WolfTurn wolf;
    wolf.Turn();
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    CHECK(cCopy.BoundReadAtStart == 0); // the owner's old value as the copy's clip started
    CHECK(cCopy.BoundReadAtEnd == 1);
    CHECK(cCopy.GraphState == "StandingIdle");
}

TEST_CASE("A copy's canned turn exits on its owner's new variable", "[animation][replay-start]")
{
    // CannedTurnState's exit reads iSyncIdleLocomotion at cannedTurnStop, once (the duo's stuck copy, docs/bugs.md).
    WolfTurn wolf({}, true);
    wolf.Turn();
    CHECK(wolf.guest.Actors.at(wolf.copy).BoundReadAtStart == 0);
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
}

TEST_CASE("A copy's canned turns end where its owner's do over a slow line", "[animation][replay-start]")
{
    // net_check's slow profile: a lost turn resent ends the copy's clip late in its playback; the next turn starts
    // from StandingIdle.
    WolfTurn wolf(NetConditions{120, 80, 10, 2}, true);
    wolf.Turn();
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
    wolf.party.As(wolf.party.Host(), [&] { wolf.host.Actors.at(wolf.wolf).GraphVariables.Integers = {0, 2}; });
    wolf.Turn();
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
}

TEST_CASE("A copy's machine reads its owner's new variable with its owner's next action right behind it", "[animation][replay-start]")
{
    // Skyrim (duo, 2026-11-08): a wolf sends its next action just after its turn's end, stamped before the snapshot
    // carrying iSyncTurnState: the refused turnStop before it carries the write.
    WolfTurn wolf({}, true);
    for (auto* engine : {&wolf.host, &wolf.guest})
        for (const auto* cState : {"StandingIdle", "LoopingTurnRight", "Standing", "CannedTurn"})
            engine->GraphTransitions[{cState, "sniff"}] = cState;
    ActionEvent sniff;
    sniff.ActionId = 0x13010;
    sniff.EventName = "sniff";
    wolf.Turn([] {}, [&] {
        // 50 ms on: after the copy's own clip end (its turn replayed a few frames late), before the next snapshot.
        const uint64_t cEnd = wolf.party.Host().GetFrameTick();
        wolf.party.Until([&] { return wolf.party.Host().GetFrameTick() >= cEnd + 50; }, "50 ms after the owner's turn", {}, &wolf.party.Host());
        wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(wolf.wolf, sniff)); });
    });
    CHECK(wolf.guest.Actors.at(wolf.copy).BoundReadAtEnd == 1);
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
}

TEST_CASE("A copy's canned turn ends where its owner's does as its playback slows after a fight", "[animation][replay-start]")
{
    // Out of combat its owner's send interval goes from 33 to 100 ms; the copy's playback steers onto it at 0.9x
    // (CopyPlayback::kSteer) while its clips run at the frames' time.
    WolfTurn wolf({}, true);
    wolf.party.As(wolf.party.Host(), [&] { wolf.host.Actors.at(wolf.wolf).Ai.CombatState = 1; });
    const auto cInterval = [&] {
        for (auto&& [cEntity, cForm, cInterpolation] : wolf.party.Guest().view<FormIdComponent, InterpolationComponent>().each())
            if (cForm.Id == wolf.copy)
                return cInterpolation.Playback.Interval();
        return 0.0;
    };
    wolf.party.Until([&] { return cInterval() < 40.0; }, "the copy's playback steers onto its fighting owner's 30 a second", {}, nullptr,
                     std::chrono::seconds{10});
    wolf.party.As(wolf.party.Host(), [&] { wolf.host.Actors.at(wolf.wolf).Ai.CombatState = 0; });
    wolf.Turn();
    CHECK(wolf.guest.Actors.at(wolf.copy).BoundReadAtStart == 0);
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
}

TEST_CASE("A copy's turn paused with its graph reads its owner's integers as it resumes", "[animation][replay-start]")
{
    // Its graph stops updating mid-clip (KillImpl's flag, an unloaded actor) while playback runs on.
    WolfTurn wolf({}, true);
    wolf.Turn([&] {
        wolf.guest.Actors.at(wolf.copy).GraphUpdating = false;
        const uint64_t cFrom = wolf.party.Guest().GetFrameTick();
        wolf.party.Until([&] { return wolf.party.Guest().GetFrameTick() > cFrom + 800; }, "the copy's graph stands still for 800 ms");
        wolf.guest.Actors.at(wolf.copy).GraphUpdating = true;
    });
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
}

TEST_CASE("A copy's replayed action is sent with its own integers, not the next snapshot's", "[animation][replay-start]")
{
    // An action loads its variables one graph update before its event (ReplayStep.h). The owner let go of block
    // (iWantBlock 0) just after it: that snapshot, within an interval, must not reach the graph in between.
    WolfTurn wolf;
    wolf.party.As(wolf.party.Host(), [&] { wolf.host.Actors.at(wolf.wolf).GraphVariables.Integers = {0, 2, 1}; });
    wolf.party.Until([&] { const auto& cInts = wolf.guest.Actors.at(wolf.copy).GraphVariables.Integers; return cInts.size() == 3 && cInts[2] == 1; },
                     "the copy has iWantBlock 1");
    for (auto* engine : {&wolf.host, &wolf.guest})
        engine->GraphTransitions[{"Standing", "blockStart"}] = "Blocking";
    ActionEvent block;
    block.ActionId = 0x13005;
    block.EventName = "blockStart";
    wolf.party.As(wolf.party.Host(), [&] {
        wolf.host.Actors.at(wolf.wolf).GraphVariables.Integers = {0, 2, 1};
        REQUIRE(wolf.host.PerformAnimation(wolf.wolf, block));
        wolf.host.Actors.at(wolf.wolf).GraphVariables.Integers = {0, 2, 0};
    });
    wolf.party.Until([&] { return wolf.guest.Actors.at(wolf.copy).GraphState == "Blocking"; }, "the copy takes the block");
    REQUIRE(wolf.guest.Actors.at(wolf.copy).ReplayedOn.Integers.size() == 3);
    CHECK(wolf.guest.Actors.at(wolf.copy).ReplayedOn.Integers[2] == 1);
    wolf.party.Until([&] { return wolf.guest.Actors.at(wolf.copy).GraphVariables.Integers[2] == 0; }, "then the copy has the owner's release");
}
