#include "Party.h"
#include "Fixtures.h"
#include <Services/CharacterService.h>
#include <Messages/NotifyReferencesMove.h>
#include <Systems/InterpolationSystem.h>
#include <Components/InterpolationComponent.h>
#include <SessionIds.h>
#include <Games/Animation/ReplayState.h>

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
    // The copy's graph takes its owner's steps and seed from its stream.
    for (auto* engine : {&host, &guest})
    {
        engine->Loop = FakeEngine::GraphLoop{"CounterIdle", "CounterArms", 4000, 3};
        engine->Graph = FakeGraphRules{};
    }
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

TEST_CASE("A copy arriving mid-loop is in its owner's loop from its sync point", "[animation][replay-points]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    // Skyrim (Lucan's counter, a second visit): the copy came up a loop ahead, in the other clip, and stayed there. Its
    // spawn's graph state holds no clip time or loop count; its stream's sync point does (Havok's internal state).
    for (auto* engine : {&host, &guest})
    {
        engine->Loop = FakeEngine::GraphLoop{"CounterIdle", "CounterArms", 4000, 2};
        engine->Graph = FakeGraphRules{};
    }
    uint32_t lucan = 0;
    party.As(party.Host(), [&] {
        lucan = host.LoadNpc(0x13487, 0x31D2E, {});
        auto& owner = host.Actors.at(lucan);
        owner.GraphState = "CounterArms";
        owner.GraphRandom = 0x7DE6C53E;
        owner.LoopFrames = 700;
        owner.LoopsSeen = 1;
        owner.LoopsToSend = 2;
    });
    party.Until([&] { return guest.CopiesOf(0x13487).size() == 1; }, "Lucan's copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x13487).front();
    party.Until([&] { return party.Guest().GetGraphStreams().OnStream(cCopy); }, "the copy is on its owner's stream");
    const auto& copied = guest.Actors.at(cCopy);
    const auto& cOwner = host.Actors.at(lucan);
    const size_t cFrom = copied.LoopDraws.size();
    const size_t cOwnerFrom = cOwner.LoopDraws.size();
    uint32_t apart = 0, frames = 0;
    party.Until([&] {
        apart += copied.GraphState != cOwner.GraphState;
        ++frames;
        return cOwner.LoopDraws.size() >= cOwnerFrom + 4;
    }, "the owner picks four times", {}, nullptr, std::chrono::seconds{60});
    REQUIRE(copied.LoopDraws.size() >= cFrom + 3);
    const std::vector<uint32_t> cLast(copied.LoopDraws.end() - 3, copied.LoopDraws.end());
    CHECK(std::ranges::search(cOwner.LoopDraws, cLast).begin() != cOwner.LoopDraws.end());
    // Frames against a delay in real time: a loaded machine reads more apart. Out of step reads 38% and up.
    CHECK(apart * 4 < frames);
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
            // CannedTurnState's clip; StandingBehavior starts from iSyncTurnState (integer 0).
            engine->Graph = FakeGraphRules{.Clips = {{.State = "CannedTurn", .Ms = 1000, .Starts = {"LoopingTurnRight", "StandingIdle"}, .StartFrom = 0}}};
            engine->OwnerClipEnds = FakeEngine::ClipEndActions{"CannedTurn", {"turnStop"}};
            engine->ActionWrites = {{"turnStop", {0, 1}}, {"moveStop", {1, 0}}};
            if (aExitCondition)
            {
                engine->OwnerClipEnds->Events.push_back("moveStop");
                engine->Graph->Clips[0].ExitVariable = 1; // iSyncIdleLocomotion
                engine->Graph->Clips[0].ExitValues = {0, 1};
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
        party.Until([&] { return party.Guest().GetGraphStreams().OnStream(copy); }, "the copy's graph is on its owner's stream");
    }

    // aWhileTurning: once the copy took the turn, before its clip ends. aOwnerEnded: as the owner's clip ends.
    void Turn(const std::function<void()>& aWhileTurning = {}, const std::function<void()>& aOwnerEnded = {})
    {
        ActionEvent turn;
        turn.ActionId = 0x13002;
        turn.EventName = "cannedTurnRight180";
        party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(wolf, turn)); });
        party.Until([&] { return host.Actors.at(wolf).GraphState == "StandingIdle"; }, "the owner's canned turn ends standing");
        if (aOwnerEnded)
            aOwnerEnded();
        // Its graph's event, from its owner's stream (the action's own message can come first).
        party.Until([&] { return guest.Actors.at(copy).GraphState == "CannedTurn"; }, "the copy's graph takes the turn");
        if (aWhileTurning)
            aWhileTurning();
        party.Until([&] { return guest.Actors.at(copy).GraphState != "CannedTurn"; }, "the copy's canned turn ends");
    }

    // The live check (FakeGraph.h): empty while the copy's graph has been in its owner's state at every point.
    [[nodiscard]] std::string Difference() const
    {
        return FirstGraphDifference(host.Actors.at(wolf).Graph.History, guest.Actors.at(copy).Graph.History);
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
    CHECK(cCopy.Graph.ReadAtStart == 0); // the owner's old value as the copy's clip started
    CHECK(cCopy.Graph.ReadAtEnd == 1);
    CHECK(cCopy.GraphState == "StandingIdle");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A far owner on the stream steps every frame, and its canned turn ends where its copy's does", "[animation][replay-start]")
{
    // Skyrim (duo trace, 2026-11-08): the host's wolf, far from the host's camera, stepped its graph 0.08-0.25 s at a
    // time (animation interpolation, 63587) while its copy stepped every frame, and its turn ended up to a chunk after
    // its copy's. A recorded owner is not interpolated (the hook's interpAllowed): it steps every frame, as its copy.
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->FarOwnerChunkMs = 250.0; // fAnimInterpMaxTime
        engine->Graph->Clips[0].Ms = 1267; // TurnCannedR180
    }
    const auto& cOwner = wolf.host.Actors.at(wolf.wolf);
    double longest = 0.0;
    const auto cStep = [&] { longest = std::max(longest, cOwner.AnimationStepMs); };
    wolf.Turn(cStep, cStep);
    wolf.party.Until([&] { cStep(); return cOwner.AnimationMs > 3000.0; }, "the owner's graph runs on");
    CHECK(longest < 100.0);
    CHECK(wolf.guest.Actors.at(wolf.copy).Graph.ReadAtEnd == 1);
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A far owner's damped turn picks its walk where its copy's does", "[animation][replay-start]")
{
    // Skyrim (duo, Riverwood, 2026-11-09): the copy's update wrote a different TurnDeltaDamped (a damping modifier's
    // member, moved in each generate pass, 63568) and its walk picked WalkForwardR where its owner's did not. A graph
    // generates after each frame it updated in (63589): the owner once an update, the copy once a frame, whatever it
    // replayed in it. The copy runs its owner's generate passes instead (the stream's).
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->FarOwnerChunkMs = 250.0;
        engine->Graph->TurnDeltaChannel = 0;
        engine->Graph->Damping = FakeGraphRules::Damp{.Input = 0, .Output = 1, .Gain = 2.f};
        engine->Graph->Ranges = {{.State = "Walk", .Input = 1, .Bound = 0.9f, .Above = "WalkRight"}};
        engine->GraphTransitions[{"Standing", "moveStart"}] = "Walk";
    }
    auto& owner = wolf.host.Actors.at(wolf.wolf);
    const uint64_t cFrom = wolf.party.Host().GetFrameTick();
    const float cHeading = owner.Rotation[2];
    // Its AI turns it at 1 rad/s as it walks.
    const auto cTurning = [&] { owner.Rotation[2] = cHeading + float(wolf.party.Host().GetFrameTick() - cFrom) / 1000.f; };
    ActionEvent walk;
    walk.ActionId = 0x13030;
    walk.EventName = "moveStart";
    wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(wolf.wolf, walk)); });
    wolf.party.Until([&] { cTurning(); return owner.GraphState == "WalkRight"; }, "the owner's damped turn passes its bound");
    wolf.party.Until([&] { cTurning(); return wolf.guest.Actors.at(wolf.copy).GraphState == "WalkRight"; }, "the copy's too");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A copy's canned turn exits on its owner's new variable", "[animation][replay-start]")
{
    // CannedTurnState's exit reads iSyncIdleLocomotion at cannedTurnStop, once (the duo's stuck copy, docs/bugs.md).
    WolfTurn wolf({}, true);
    wolf.Turn();
    CHECK(wolf.guest.Actors.at(wolf.copy).Graph.ReadAtStart == 0);
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A copy's canned turns end where its owner's do over a slow line", "[animation][replay-start]")
{
    // net_check's slow profile: the copy's playback steers with the jitter while its clips run at the frames' time. Its
    // owner's refused turnStop, 40 ms before the clip's end, comes as far after the turn on the copy's graph time
    // (ActionDue), not its playback's.
    WolfTurn wolf(NetConditions{120, 80, 10, 2}, true);
    wolf.Turn();
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
    CHECK(wolf.Difference() == "");
    wolf.party.As(wolf.party.Host(), [&] { wolf.host.Actors.at(wolf.wolf).GraphVariables.Integers = {0, 2}; });
    wolf.Turn();
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
    CHECK(wolf.Difference() == "");
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
        wolf.party.Until([&] { return wolf.party.Host().GetFrameTick() >= cEnd + 50; }, "50 ms after the owner's turn");
        wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(wolf.wolf, sniff)); });
    });
    CHECK(wolf.guest.Actors.at(wolf.copy).Graph.ReadAtEnd == 1);
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A copy's canned turn ends where its owner's does as its playback slows after a fight", "[animation][replay-start]")
{
    // Out of combat its owner's send interval goes from 33 to 100 ms; the copy's playback steers onto it at 0.9x
    // (CopyPlayback::kSteer) while its clips run at the frames' time: on playback, the turnStop came after the clip's end.
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
    CHECK(wolf.guest.Actors.at(wolf.copy).Graph.ReadAtStart == 0);
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
    CHECK(wolf.Difference() == "");
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
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A copy's graph takes its owner's event with its owner's integers, not the next snapshot's", "[animation][replay-start]")
{
    // The owner let go of block (iWantBlock 0) just after it: the copy's graph must take the event with 1, as its
    // owner's did (on the stream: the event's op carries its variables, the release goes with the next update's).
    WolfTurn wolf;
    wolf.party.As(wolf.party.Host(), [&] { wolf.host.Actors.at(wolf.wolf).GraphVariables.Integers = {0, 2, 1}; });
    wolf.party.Until([&] { const auto& cInts = wolf.guest.Actors.at(wolf.copy).GraphVariables.Integers; return cInts.size() >= 3 && cInts[2] == 1; },
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
    REQUIRE(wolf.guest.Actors.at(wolf.copy).Graph.EventWords.size() > 2);
    CHECK(wolf.guest.Actors.at(wolf.copy).Graph.EventWords[2] == 1);
    wolf.party.Until([&] { return wolf.guest.Actors.at(wolf.copy).GraphVariables.Integers[2] == 0; }, "then the copy has the owner's release");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A copy's quick turn picks its owner's speed", "[animation][replay-start]")
{
    // A range modifier (hkbEventsFromRangeModifier, TurnRight_EventsFromRangeMod in 0_master) picks a turn's speed from
    // TurnDelta, a channel each game's graph fills from its own actor before each update (63590, read in IDA). A copy's
    // heading is interpolated between snapshots, so a quick turn of its owner's is a slower one on it. Not yet seen on
    // the duo: the turn speed differences seen there were the range modifier's clone bug (graph-input-replay.md).
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->Graph->TurnDeltaChannel = 0;
        engine->Graph->Ranges = {{.State = "TurnRight", .Input = 0, .Bound = 2.f, .Above = "TurnRightFast", .Below = "TurnRightSlow"}};
        engine->GraphTransitions[{"Standing", "turnRight"}] = "TurnRight";
    }
    ActionEvent turn;
    turn.ActionId = 0x13020;
    turn.EventName = "turnRight";
    auto& owner = wolf.host.Actors.at(wolf.wolf);
    const float cHeading = owner.Rotation[2];
    const uint64_t cFrom = wolf.party.Host().GetFrameTick();
    // Its AI turns it at 3 rad/s for 60 ms, and sends turnRight 30 ms in.
    const auto cTurning = [&] {
        owner.Rotation[2] = cHeading + 3.f * float(std::min<uint64_t>(wolf.party.Host().GetFrameTick() - cFrom, 60)) / 1000.f;
    };
    wolf.party.Until([&] { cTurning(); return wolf.party.Host().GetFrameTick() >= cFrom + 30; }, "30 ms into the owner's turn");
    wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(wolf.wolf, turn)); });
    wolf.party.Until([&] { cTurning(); return owner.GraphState == "TurnRightFast"; }, "the owner turns fast");
    wolf.party.Until([&] { cTurning(); const auto& cState = wolf.guest.Actors.at(wolf.copy).GraphState; return cState != "Standing" && cState != "TurnRight"; },
                     "the copy's turn picks a speed");
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "TurnRightFast");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("The live graph check compares a copy with its owner at the same owner time", "[animation][replay-start]")
{
    using E = FakeGraphMemory::Entry;
    const std::vector<E> cOwner{{1, "A", "update"}, {2, "A", "update"}, {2, "B", "event go"}, {3, "B", "update"}, {4, "C", "update"}};
    // From a sync point at 2, the same inputs: the update and the event at 2 matched in their order.
    CHECK(FirstGraphDifference(cOwner, {{2, "A", "load"}, {2, "B", "event go"}, {3, "B", "update"}, {4, "C", "update"}}) == "");
    // Before its first sync point it ran on its own (the old paths): from there on, compared.
    CHECK(FirstGraphDifference(cOwner, {{0, "A", "load"}, {3, "A", "update"}, {2, "A", "sync point"}, {2, "B", "event go"}, {3, "B", "update"}}) == "");
    // Behind its owner: what it has run so far agrees.
    CHECK(FirstGraphDifference(cOwner, {{2, "A", "load"}, {2, "B", "event go"}}) == "");
    // Its event a step late.
    CHECK(FirstGraphDifference(cOwner, {{2, "A", "load"}, {3, "A", "update"}, {3, "B", "event go"}}).starts_with("at owner time 3.0 ms the copy is in A"));
    // Never on its owner's stream: nothing to compare is no pass.
    CHECK(FirstGraphDifference(cOwner, {{0, "A", "load"}, {1, "A", "own update"}}).starts_with("nothing compared"));
    // A different pick.
    CHECK(FirstGraphDifference(cOwner, {{2, "A", "load"}, {2, "B", "event go"}, {3, "B", "update"}, {4, "D", "update"}}).contains("its owner in C"));
}

TEST_CASE("A copy on its owner's stream keeps it when its spawn's graph state could load only later", "[animation][replay-start]")
{
    // Skyrim (duo, Riverwood, 2026-11-09): a copy whose graph could take its spawn's state only after its stream had
    // started loaded that state over the stream's, back in the chair it spawned in (AnimationSystem loaded it first).
    Party party(1, SmokeWorld, true);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    for (auto* engine : {&host, &guest})
    {
        engine->Graph = FakeGraphRules{};
        engine->GraphTransitions = {{{"Sitting", "standUp"}, "Standing"}};
    }
    guest.NewCopiesGraphReady = false;
    uint32_t npc = 0;
    party.As(party.Host(), [&] {
        npc = host.LoadNpc(0x23AB2, 0x31D2E, {});
        host.Actors.at(npc).GraphState = "Sitting";
    });
    party.Until([&] { return guest.CopiesOf(0x23AB2).size() == 1; }, "the copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x23AB2).front();
    party.Until([&] { return party.Guest().GetGraphStreams().OnStream(cCopy) && guest.Actors.at(cCopy).GraphState == "Sitting"; },
                "the copy's graph starts on its owner's stream");
    ActionEvent standUp;
    standUp.ActionId = 0x13030;
    standUp.EventName = "standUp";
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(npc, standUp)); });
    party.Until([&] { return guest.Actors.at(cCopy).GraphState == "Standing"; }, "the copy stands up with its owner");
    guest.Actors.at(cCopy).GraphReady = true; // its spawn's state could load now
    const uint64_t cFrom = party.Guest().GetFrameTick();
    party.Until([&] { return party.Guest().GetFrameTick() > cFrom + 200; }, "200 ms on");
    CHECK(guest.Actors.at(cCopy).GraphState == "Standing");
    CHECK(FirstGraphDifference(host.Actors.at(npc).Graph.History, guest.Actors.at(cCopy).Graph.History) == "");
}

namespace
{
size_t SyncPoints(const FakeEngine::Actor& acActor)
{
    return size_t(std::ranges::count(acActor.Graph.History, std::string("sync point"), &FakeGraphMemory::Entry::Input));
}
} // namespace

TEST_CASE("A copy whose graph is replaced starts again from a new sync point", "[animation][replay-start]")
{
    // Skyrim (duo, Riverwood, 2026-11-09): a mudcrab's copy matched its owner for 3 updates, then had no state at all:
    // its graph was replaced (its 3D) after its stream started, and the stream ran on into a graph it never loaded.
    WolfTurn wolf({}, true);
    wolf.guest.ReloadGraph(wolf.copy, "Sitting");
    wolf.party.Until([&] { return SyncPoints(wolf.guest.Actors.at(wolf.copy)) >= 2 && wolf.party.Guest().GetGraphStreams().OnStream(wolf.copy); },
                     "the copy's new graph starts from a new sync point");
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "Standing");
    wolf.Turn();
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("An owner whose graph is replaced gives its copy a new sync point", "[animation][replay-start]")
{
    WolfTurn wolf({}, true);
    wolf.party.As(wolf.party.Host(), [&] { wolf.host.ReloadGraph(wolf.wolf, "StandingIdle"); });
    wolf.party.Until([&] { return SyncPoints(wolf.guest.Actors.at(wolf.copy)) >= 2 && wolf.party.Guest().GetGraphStreams().OnStream(wolf.copy); },
                     "the copy starts again from its owner's new graph");
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
    wolf.Turn();
    CHECK(wolf.guest.Actors.at(wolf.copy).GraphState == "StandingIdle");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A copy with another project's graph than its owner's loads no point until its own graph is its owner's", "[animation][replay-start]")
{
    // A copy made from a leveled base not yet its owner's has another race's graph: Havok's restore (58373) would
    // write the owner's nodes into it. The point names its project; the copy asks again only with a new graph.
    WolfTurn wolf({}, true);
    const auto& cOwner = wolf.host.Actors.at(wolf.wolf);
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    wolf.guest.ReloadGraph(wolf.copy, "Standing", 2);
    const double cFrom = cOwner.AnimationMs;
    wolf.party.Until([&] { return cOwner.AnimationMs > cFrom + 1000.0; }, "its owner's point comes and a second passes");
    CHECK(cCopy.ForeignPoints == 0);
    CHECK(SyncPoints(cCopy) == 1);
    CHECK_FALSE(wolf.party.Guest().GetGraphStreams().OnStream(wolf.copy));
    wolf.guest.ReloadGraph(wolf.copy, "Standing", 1);
    wolf.party.Until([&] { return SyncPoints(cCopy) >= 2 && wolf.party.Guest().GetGraphStreams().OnStream(wolf.copy); },
                     "the copy's graph of its owner's project starts from a new sync point");
    wolf.Turn();
    CHECK(cCopy.ForeignPoints == 0);
    CHECK(cCopy.GraphState == "StandingIdle");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A copy starting mid-blend plays its owner's child, not the one its variables pick now", "[animation][replay-start]")
{
    // Skyrim (duo, Riverwood, 2026-11-09): Faendal's copy parted from its owner at its first update. Its turn blend,
    // activated afresh as the sync point loaded, chose its own child from the owner's variables of that moment; the
    // owner's had chosen MT_TurnLeft360_Slow as it activated, before they changed (docs/re/systems/animation-replay-recovery.md).
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->Graph->Blends = {{.State = "TurnBlend", .ChooseFrom = 2, .Children = {"TurnLeft", "TurnRight"}}};
        engine->GraphTransitions[{"Standing", "turnStart"}] = "TurnBlend";
    }
    ActionEvent turn;
    turn.ActionId = 0x13003;
    turn.EventName = "turnStart";
    wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(wolf.wolf, turn)); });
    const auto& cOwner = wolf.host.Actors.at(wolf.wolf);
    wolf.party.Until([&] { return cOwner.Graph.Child == "TurnLeft"; }, "the owner's blend chooses its child as it activates");
    wolf.party.As(wolf.party.Host(), [&] {
        auto& integers = wolf.host.Actors.at(wolf.wolf).GraphVariables.Integers;
        integers.resize(3);
        integers[2] = 1; // its AI's new turn speed: the blend keeps its child
    });
    wolf.guest.ReloadGraph(wolf.copy, "Standing");
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    wolf.party.Until([&] { return SyncPoints(cCopy) >= 2 && wolf.party.Guest().GetGraphStreams().OnStream(wolf.copy) && !cCopy.Graph.Child.empty(); },
                     "the copy's new graph starts from a sync point taken mid-blend");
    CHECK(cOwner.Graph.Child == "TurnLeft");
    CHECK(cCopy.Graph.Child == "TurnLeft");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A copy starting between its owner's generate pass and its next update takes the blend's new child with it", "[animation][replay-start]")
{
    // Skyrim (duo, Whiterun, 2026-11-09): copy 37081 parted from its owner at its first update: the owner's walk blend
    // dropped MT_WalkForwardRightSlow there, the copy's kept it. The owner's generate pass before the sync point had
    // picked the blend's new children (59540) and set stateOrTransitionChanged, which makes the next update walk the
    // active nodes (58329, 60079); Havok's restore clears it. The point carries it.
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->FarOwnerChunkMs = 250.0;
        engine->Graph->Blends = {{.State = "Walk", .ChooseFrom = 2, .Children = {"Forward", "Right"}, .Generated = true}};
        engine->GraphTransitions[{"Standing", "moveStart"}] = "Walk";
    }
    ActionEvent walk;
    walk.ActionId = 0x13030;
    walk.EventName = "moveStart";
    wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(wolf.wolf, walk)); });
    const auto& cOwner = wolf.host.Actors.at(wolf.wolf);
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    wolf.party.Until([&] { return cCopy.Graph.Child == "Forward"; }, "the copy walks forward with its owner");
    // Its AI turns it one way, then the other: each generate pass picks a new child, so every update starts flagged.
    const auto cTurning = [&] {
        auto& integers = wolf.host.Actors.at(wolf.wolf).GraphVariables.Integers;
        integers.resize(3);
        integers[2] = (cOwner.Graph.Child == "Right") == cOwner.Graph.ActiveNodesChanged ? 1 : 0;
    };
    wolf.party.Until([&] { cTurning(); return cOwner.Graph.ActiveNodesChanged && cCopy.Graph.Child == "Right"; },
                     "the owner's generate passes pick new children, and the copy's blend follows");
    wolf.party.As(wolf.party.Guest(), [&] { wolf.party.Guest().GetGraphStreams().Forget(wolf.copy); }); // asks for a new point
    wolf.party.Until([&] { cTurning(); return SyncPoints(cCopy) >= 2 && wolf.party.Guest().GetGraphStreams().OnStream(wolf.copy); },
                     "the copy starts again from a point taken between a generate pass and its update");
    const size_t cReplayed = cCopy.Graph.History.size();
    wolf.party.Until([&] { cTurning(); return cCopy.Graph.History.size() >= cReplayed + 8; }, "the copy replays on from it");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A copy starting while its owner's walk blend is frozen keeps the frozen direction", "[animation][replay-start]")
{
    // Skyrim (duo, Whiterun, 2026-11-09): copy 37081, restored as its owner started to walk out of a turn, blended
    // MT_WalkForwardLeft where its owner had Right. BSCyclicBlendTransitionGenerator had frozen its blender's
    // blendParameter (63132, mode 1): a plain member, in no internal state. The point carries nodes' plain members.
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->Graph->Blends = {{.State = "Walk", .ChooseFrom = 2, .Children = {"Forward", "Right"}, .Generated = true, .FreezeOn = "freezeBlend"}};
        engine->GraphTransitions[{"Standing", "moveStart"}] = "Walk";
        engine->GraphTransitions[{"Walk", "freezeBlend"}] = "Walk"; // in its event table
    }
    ActionEvent walk;
    walk.ActionId = 0x13030;
    walk.EventName = "moveStart";
    wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(wolf.wolf, walk)); });
    const auto& cOwner = wolf.host.Actors.at(wolf.wolf);
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    wolf.party.Until([&] { return cCopy.Graph.Child == "Forward"; }, "the copy walks forward with its owner");
    ActionEvent freeze;
    freeze.ActionId = 0x13031;
    freeze.EventName = "freezeBlend";
    wolf.party.As(wolf.party.Host(), [&] {
        REQUIRE(wolf.host.PerformAnimation(wolf.wolf, freeze));
        auto& integers = wolf.host.Actors.at(wolf.wolf).GraphVariables.Integers;
        integers.resize(3);
        integers[2] = 1; // its direction turns; the frozen blend keeps Forward
    });
    wolf.party.Until([&] { return cCopy.Graph.FrozenParameter.has_value() && cCopy.GraphVariables.Integers.size() > 2 && cCopy.GraphVariables.Integers[2] == 1; },
                     "the copy froze with its owner and has the new direction");
    wolf.party.As(wolf.party.Guest(), [&] { wolf.party.Guest().GetGraphStreams().Forget(wolf.copy); });
    wolf.party.Until([&] { return SyncPoints(cCopy) >= 2 && wolf.party.Guest().GetGraphStreams().OnStream(wolf.copy); },
                     "the copy starts again from a point taken while frozen");
    const double cFrom = cCopy.Graph.Timeline;
    wolf.party.Until([&] { return cCopy.Graph.Timeline > cFrom + 50.0; }, "the copy replays 50 ms of its owner's stream from there");
    CHECK(cOwner.Graph.Child == "Forward");
    CHECK(cCopy.Graph.Child == "Forward");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A copy asking while its owner's walk blend cross-blends gets its point after the blend", "[animation][replay-start]")
{
    // Skyrim (duo, guest-rides-host-horse, 2026-11-10): a guest crashed at 59960+0x95 in a copy's first update after
    // its sync point. Its owner's BSCyclicBlendTransitionGenerator was cross-blending (mode 2): the point carried the
    // mode, a plain member, onto a clone whose effect had no blenders (63133 links them). No point in a cross-blend.
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->Graph->Blends = {{.State = "Walk", .ChooseFrom = 2, .Children = {"Forward", "Right"}, .Generated = true,
                                  .FreezeOn = "freezeBlend", .CrossBlendOn = "crossBlend", .CrossBlendMs = 1500.0}};
        engine->GraphTransitions[{"Standing", "moveStart"}] = "Walk";
        engine->GraphTransitions[{"Walk", "freezeBlend"}] = "Walk"; // in its event table
        engine->GraphTransitions[{"Walk", "crossBlend"}] = "Walk";
    }
    const auto cPerform = [&](uint32_t aActionId, const char* apEvent) {
        ActionEvent action;
        action.ActionId = aActionId;
        action.EventName = apEvent;
        wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(wolf.wolf, action)); });
    };
    const auto& cOwner = wolf.host.Actors.at(wolf.wolf);
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    cPerform(0x13030, "moveStart");
    wolf.party.Until([&] { return cCopy.Graph.Child == "Forward"; }, "the copy walks forward with its owner");
    cPerform(0x13031, "freezeBlend");
    wolf.party.Until([&] { return cCopy.Graph.FrozenParameter.has_value(); }, "the copy froze with its owner");
    cPerform(0x13032, "crossBlend");
    REQUIRE(cOwner.Graph.CrossBlendLeftMs > 0.0);
    wolf.party.As(wolf.party.Guest(), [&] { wolf.party.Guest().GetGraphStreams().Forget(wolf.copy); });
    wolf.party.Until([&] { return SyncPoints(cCopy) >= 2 && wolf.party.Guest().GetGraphStreams().OnStream(wolf.copy); },
                     "the copy starts again from a point");
    CHECK(wolf.guest.RestoredMidCrossBlend == 0);
    CHECK(cOwner.Graph.CrossBlendLeftMs == 0.0);
    const double cFrom = cCopy.Graph.Timeline;
    wolf.party.Until([&] { return cCopy.Graph.Timeline > cFrom + 50.0; }, "the copy replays 50 ms of its owner's stream from there");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A streamed copy knocked down with its owner gets up with it", "[animation][replay-start][knock]")
{
    // Skyrim (duo, knockdown-lies-as-host, 2026-11-09): "got up: owner 1.54 s, copy never". A copy's own body stands
    // up only by its owner's ActionGetUp (AnimationSystem, KnockReplay), which a copy on the stream takes as
    // bookkeeping: that bookkeeping stands it up, and its graph's get-up is the stream's.
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
        engine->GraphTransitions[{"Standing", "GetUpBegin"}] = "Standing";
    auto& streams = wolf.party.Guest().GetGraphStreams();
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    const size_t cPoints = SyncPoints(cCopy);
    wolf.host.Actors.at(wolf.wolf).KnockedDown = true;
    wolf.guest.Actors.at(wolf.copy).KnockedDown = true; // with its owner's ragdoll (physics_flows)
    const double cLying = cCopy.Graph.Timeline;
    wolf.party.Until([&] { return cCopy.Graph.Timeline > cLying + 100.0; }, "the copy replays its owner's stream while it lies");
    ActionEvent getUp;
    getUp.ActionId = FakeEngine::kActionGetUp;
    getUp.EventName = "GetUpBegin";
    wolf.party.As(wolf.party.Host(), [&] {
        wolf.host.Actors.at(wolf.wolf).KnockedDown = false;
        REQUIRE(wolf.host.PerformAnimation(wolf.wolf, getUp));
    });
    wolf.party.Until([&] { return !cCopy.KnockedDown; }, "the copy gets up with its owner");
    CHECK(streams.OnStream(wolf.copy));
    CHECK(SyncPoints(cCopy) == cPoints); // it never left its stream
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A knocked-down copy gets up the way its owner does, on its owner's stream", "[animation][replay-start][knock][pick]")
{
    // Skyrim (duo, knockdown-lies-as-host, 2026-11-11, dev's plugin and the stream alike): "it turned away from the
    // owner's facing: 40-51 deg for 0.5-0.8 s". A get-up's clip is picked from the ragdoll's pose
    // (hkbPoseMatchingGenerator 59583), each game's own, and a knocked-down copy was off its owner's stream.
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->GraphTransitions[{"Standing", "GetUpBegin"}] = "GettingUp";
        engine->Graph->Matches = {{"GettingUp", {"FaceUp", "FaceDown"}}};
        engine->Graph->Clips.push_back({.State = "GettingUp", .Ms = 500, .Starts = {"Standing"}});
    }
    auto& streams = wolf.party.Guest().GetGraphStreams();
    const auto& cOwner = wolf.host.Actors.at(wolf.wolf);
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    const size_t cPoints = SyncPoints(cCopy);
    wolf.host.Actors.at(wolf.wolf).KnockedDown = true;
    wolf.host.Actors.at(wolf.wolf).LyingPose = 1;        // face down
    wolf.guest.Actors.at(wolf.copy).KnockedDown = true;  // with its owner's ragdoll (physics_flows)
    wolf.guest.Actors.at(wolf.copy).LyingPose = 0;       // its own ragdoll came to rest face up
    const double cLying = cCopy.Graph.Timeline;
    wolf.party.Until([&] { return cCopy.Graph.Timeline > cLying + 100.0; }, "the copy replays its owner's stream while it lies");
    CHECK(streams.OnStream(wolf.copy));
    ActionEvent getUp;
    getUp.ActionId = FakeEngine::kActionGetUp;
    getUp.EventName = "GetUpBegin";
    wolf.party.As(wolf.party.Host(), [&] {
        wolf.host.Actors.at(wolf.wolf).KnockedDown = false;
        REQUIRE(wolf.host.PerformAnimation(wolf.wolf, getUp));
    });
    wolf.party.Until([&] { return cCopy.GraphState == "GettingUp" && !cCopy.Graph.Child.empty(); }, "the copy starts getting up");
    CHECK(cOwner.Graph.Child == "FaceDown");
    CHECK(cCopy.Graph.Child == "FaceDown"); // its owner's pick, not its own ragdoll's
    wolf.party.Until([&] { return !cCopy.KnockedDown && cCopy.GraphState == "Standing"; }, "the copy is up with its owner");
    CHECK(streams.OnStream(wolf.copy));
    CHECK_FALSE(streams.Held(wolf.copy));
    CHECK(SyncPoints(cCopy) == cPoints); // it never left its stream
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A copy started after its owner's first knockdown still gets up as its owner the next time", "[animation][replay-start][knock][pick]")
{
    // Skyrim (duo, guest-knockdown-lies-on-host, 2026-11-11): the copy's get-up played no clip and it stayed in knock
    // state 6. Its owner's GetUpPoseMatcher kept its match from an earlier knockdown, so nothing was new to send, and
    // the copy's node had none: a sync point leaves an idle node out.
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->GraphTransitions[{"Standing", "GetUpBegin"}] = "GettingUp";
        engine->Graph->Matches = {{"GettingUp", {"FaceUp", "FaceDown"}}};
        engine->Graph->Clips.push_back({.State = "GettingUp", .Ms = 500, .Starts = {"Standing"}});
    }
    auto& streams = wolf.party.Guest().GetGraphStreams();
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    ActionEvent getUp;
    getUp.ActionId = FakeEngine::kActionGetUp;
    getUp.EventName = "GetUpBegin";
    const auto cKnockAndRise = [&] {
        wolf.host.Actors.at(wolf.wolf).KnockedDown = true;
        wolf.host.Actors.at(wolf.wolf).LyingPose = 1;
        wolf.guest.Actors.at(wolf.copy).KnockedDown = true;
        wolf.party.As(wolf.party.Host(), [&] {
            wolf.host.Actors.at(wolf.wolf).KnockedDown = false;
            REQUIRE(wolf.host.PerformAnimation(wolf.wolf, getUp));
        });
        wolf.party.Until([&] { return cCopy.GraphState == "GettingUp"; }, "the copy starts getting up");
    };
    cKnockAndRise();
    wolf.party.Until([&] { return cCopy.GraphState == "Standing"; }, "the copy is up");
    // A new graph for the copy (its 3D reloaded): it starts over from a sync point taken while both stand.
    const size_t cPoints = SyncPoints(cCopy);
    wolf.party.As(wolf.party.Guest(), [&] { wolf.guest.ReloadGraph(wolf.copy, "Standing"); });
    wolf.party.Until([&] { return SyncPoints(cCopy) > cPoints && streams.OnStream(wolf.copy); }, "the copy starts again from a point");
    cKnockAndRise();
    wolf.party.Until([&] { return !cCopy.Graph.Child.empty() || cCopy.GraphState != "GettingUp"; }, "its get-up plays a clip");
    CHECK(cCopy.Graph.Child == "FaceDown");
    wolf.party.Until([&] { return cCopy.GraphState == "Standing"; }, "the copy is up again");
    // A third time, lying the same way: nothing changed for its owner's node, and the copy's forgot its match.
    cKnockAndRise();
    wolf.party.Until([&] { return !cCopy.Graph.Child.empty() || cCopy.GraphState != "GettingUp"; }, "its third get-up plays a clip");
    CHECK(cCopy.Graph.Child == "FaceDown");
    wolf.party.Until([&] { return cCopy.GraphState == "Standing"; }, "the copy is up a third time");
    CHECK(wolf.Difference() == "");
}

TEST_CASE("A guest's kill move takes a streamed copy off its stream until the pair ends", "[animation][replay-start][kill-move]")
{
    // Skyrim (duo, client-kill-move, 2026-11-09): the guest's blow killed the wolf plainly. The pair's start plays the
    // victim's half through its graph (32803 -> 63591), and a copy on its owner's stream refused every event but its
    // owner's, so the pair failed. A copy in a pair on this game plays its half on its own body, off the stream.
    WolfTurn wolf;
    ActionEvent kill;
    kill.ActionId = 0x13005;
    kill.EventName = "pa_KillMoveI";
    kill.TargetEventName = "KillMoveI";
    kill.TargetId = wolf.copy;
    auto& streams = wolf.party.Guest().GetGraphStreams();
    bool started = false;
    wolf.party.As(wolf.party.Guest(), [&] { started = wolf.guest.PerformAnimation(FakeEngine::kPlayer, kill); });
    REQUIRE(started);
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    CHECK(cCopy.InKillMove);
    CHECK(streams.Held(wolf.copy));
    CHECK_FALSE(streams.OnStream(wolf.copy));
    // The pair ends without a kill: the copy asks for a new point and follows its owner again.
    const size_t cPoints = SyncPoints(cCopy);
    wolf.party.As(wolf.party.Guest(), [&] {
        wolf.guest.EndKillMove(wolf.copy);
        wolf.guest.StopPairClip(wolf.copy);
    });
    wolf.party.Until([&] { return !streams.Held(wolf.copy) && streams.OnStream(wolf.copy) && SyncPoints(cCopy) > cPoints; },
                     "the copy is back on its owner's stream from a new sync point");
}

TEST_CASE("A streamed owner's movement carries no graph variables, and its copy's graph takes its stream's alone", "[animation][replay-start]")
{
    // Phase 3 (graph-input-stream.md): a streamed copy's variables are its stream's; the snapshot's were loaded into
    // its graph every frame (ApplyRemoteMovement) and restored away before each op.
    WolfTurn wolf;
    entt::entity entity{entt::null};
    for (auto&& [id, form, interpolation] : wolf.party.Guest().view<FormIdComponent, InterpolationComponent>().each())
        if (form.Id == wolf.copy)
            entity = id;
    REQUIRE(wolf.party.Guest().valid(entity));
    auto& points = wolf.party.Guest().get<InterpolationComponent>(entity).TimePoints;
    const auto cWithVariables = [&] {
        return std::ranges::count_if(points, [](const auto& acPoint) {
            return !acPoint.Variables.Booleans.empty() || !acPoint.Variables.Integers.empty() || !acPoint.Variables.Floats.empty();
        });
    };
    uint64_t last = 0;
    int fresh = 0;
    wolf.party.Until([&] {
        if (!points.empty() && points.back().Tick != last)
        {
            last = points.back().Tick;
            ++fresh;
        }
        return fresh > 20;
    }, "twenty snapshots of the streamed owner arrive");
    CHECK(cWithVariables() == 0);
    // Its graph still turns with its owner's, from the stream.
    wolf.Turn();
}

TEST_CASE("Each player's copy is on its owner's graph stream on every other game", "[animation][replay-start][players]")
{
    // Phase 4 (graph-input-stream.md): a game records its own player's graph as it does its NPCs'; the host passes a
    // guest's stream on to the other games by the actor's owner.
    Party party(2, SmokeWorld, true);
    std::array<World*, 3> games{&party.Host(), &party.Guest(0), &party.Guest(1)};
    for (auto* game : games)
    {
        auto& engine = Party::Engine(*game);
        engine.Graph = FakeGraphRules{.Clips = {{.State = "Swing", .Ms = 300, .Starts = {"Standing"}}}};
        engine.GraphTransitions = {{{"Standing", "attackStart"}, "Swing"}};
        engine.Actors.at(FakeEngine::kPlayer).GraphState = "Standing";
    }
    // aIn's copy of aOf's player.
    const auto cCopy = [&](World& aIn, World& aOf) {
        uint32_t session = 0, copy = 0;
        party.As(aOf, [&] { session = Utils::GetSessionIdByFormId(FakeEngine::kPlayer).value_or(0); });
        if (session)
            party.As(aIn, [&] { copy = Utils::GetFormIdBySessionId(session); });
        return copy;
    };
    for (auto* owner : games)
        for (auto* other : games)
        {
            if (owner == other)
                continue;
            party.Until([&] { const uint32_t cId = cCopy(*other, *owner); return cId && other->GetGraphStreams().OnStream(cId); },
                        "a player's copy starts on its owner's stream");
        }
    for (auto* owner : games)
    {
        ActionEvent swing;
        swing.ActionId = 0x13005;
        swing.EventName = "attackStart";
        party.As(*owner, [&] { REQUIRE(Party::Engine(*owner).PerformAnimation(FakeEngine::kPlayer, swing)); });
        const auto& cOwner = Party::Engine(*owner).Actors.at(FakeEngine::kPlayer);
        party.Until([&] { return cOwner.GraphState == "Standing"; }, "its swing's clip ends");
        for (auto* other : games)
        {
            if (owner == other)
                continue;
            const uint32_t cId = cCopy(*other, *owner);
            const auto& cCopied = Party::Engine(*other).Actors.at(cId);
            party.Until([&] { return cCopied.Graph.Timeline >= cOwner.Graph.Timeline - 500 && cCopied.GraphState == "Standing"; },
                        "its copy played the swing through");
            CHECK(other->GetGraphStreams().OnStream(cId));
            CHECK(FirstGraphDifference(cOwner.Graph.History, cCopied.Graph.History) == "");
            CHECK(Party::Engine(*other).GetActorLatestAnimation(cId).EventName == "attackStart"); // bookkeeping still
        }
    }
}

TEST_CASE("A host's kill move takes its victim's copy off its stream until the owner's pair ends", "[animation][replay-start][kill-move]")
{
    // Skyrim (duo, host-kill-move, 2026-11-09): the guest's blow landed 20-70 ms before the host's. A replayed pair
    // with a copy partner makes no instance on the guest (PlayReplayedAction names no target event), so the wolf's
    // copy stayed on its stream and took the kill move's event from it: its synchronized clip started at once, where
    // its owner's waits for the mark in its instance. Its half is its owner's action, as before the stream.
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
        engine->GraphTransitions[{"Standing", "KillMoveI"}] = "KillMove";
    auto& streams = wolf.party.Guest().GetGraphStreams();
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    const size_t cPoints = SyncPoints(cCopy);
    ActionEvent kill;
    kill.ActionId = 0x13005;
    kill.EventName = "pa_KillMoveI";
    kill.TargetEventName = "KillMoveI";
    kill.TargetId = wolf.wolf;
    wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(FakeEngine::kPlayer, kill)); });
    REQUIRE(wolf.host.Actors.at(wolf.wolf).InPair);
    wolf.party.Until([&] { return cCopy.GraphState == "KillMove"; }, "the copy plays its half");
    CHECK_FALSE(cCopy.InPair); // no instance on the guest
    CHECK(streams.Held(wolf.copy));
    CHECK_FALSE(streams.OnStream(wolf.copy));
    wolf.party.Until([&] { return wolf.guest.GetActorLatestAnimation(wolf.copy).EventName == "KillMoveI"; }, "by its owner's action");
    // While its owner's pair lasts no point comes, and it stays off the stream.
    const uint64_t cFrom = wolf.party.Guest().GetTick();
    wolf.party.Until([&] { return wolf.party.Guest().GetTick() >= cFrom + 1000; }, "the owner's pair runs on a second");
    CHECK(SyncPoints(cCopy) == cPoints);
    CHECK_FALSE(streams.OnStream(wolf.copy));
    // The pair ends without a kill: its owner is recorded again, and the copy follows from a new point.
    wolf.party.As(wolf.party.Host(), [&] {
        wolf.host.EndKillMove(wolf.wolf);
        wolf.host.StopPairClip(wolf.wolf);
    });
    wolf.party.Until([&] { return !streams.Held(wolf.copy) && streams.OnStream(wolf.copy) && SyncPoints(cCopy) > cPoints; },
                     "the copy is back on its owner's stream from a new sync point");
}

TEST_CASE("A copy held for its owner's paired action is back on its stream only after it played it", "[animation][replay-start][players]")
{
    // Skyrim (duo, host-rides-horse, 2026-11-10): a rider's copy never got off. Getting off is a paired action with no
    // instance on its owner's game, so its owner went on recording and answered the copy's request at once: the copy
    // was back on its stream before the action's clip files were in, and the action's event was then dropped.
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->GraphTransitions[{"Standing", "HorseExit"}] = "Dismounted";
        engine->ClipFiles.insert({"HorseExit", "Dismounted"});
    }
    wolf.guest.ClipLoadFrames = 400; // the action waits for its clip files on the copy's game
    auto& streams = wolf.party.Guest().GetGraphStreams();
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    ActionEvent exit;
    exit.ActionId = 0x13006;
    exit.EventName = "HorseExit";
    exit.TargetEventName = "HorseExitPartner";
    exit.TargetId = 0; // its partner is not here: no instance, its owner is recorded on
    wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(wolf.wolf, exit)); });
    REQUIRE_FALSE(wolf.host.Actors.at(wolf.wolf).InPair);
    wolf.party.Until([&] { return wolf.guest.GetActorLatestAnimation(wolf.copy).EventName == "HorseExit"; }, "the copy played the action", [&] {
        return std::format("held {} on stream {} state {}", streams.Held(wolf.copy), streams.OnStream(wolf.copy), cCopy.GraphState);
    });
    CHECK(cCopy.GraphState == "Dismounted");
    wolf.party.Until([&] { return !streams.Held(wolf.copy) && streams.OnStream(wolf.copy); }, "then it is back on its owner's stream");
}

TEST_CASE("A rider's copy is back on its stream only once its own getting off has ended", "[animation][replay-start][players]")
{
    // Skyrim (duo, guest-rides-host-horse, 2026-11-10): a rider's copy stayed on the horse. A copy gets off by its
    // owner's HorseExit action alone: no Actor::Dismount (37906) marks it getting off, and only its clip's own exit
    // event (ChairFurnitureExitHandler 42835) unlinks it. Its owner's sync point came 2.4 s after the action, the
    // copy's clip not over: on its stream again with sit state 4 (wants to stand), riding for good.
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->GraphTransitions[{"Standing", "HorseExit"}] = "Dismounted";
        engine->ClipFiles.insert({"HorseExit", "Dismounted"});
        engine->ExitClips["HorseExit"] = 3000;
    }
    auto& streams = wolf.party.Guest().GetGraphStreams();
    wolf.guest.Mounts[wolf.copy] = 0x1234;
    ActionEvent exit;
    exit.ActionId = 0x13006;
    exit.EventName = "HorseExit";
    exit.TargetEventName = "HorseExitPartner";
    exit.TargetId = 0; // its mount is no actor of this fake: no instance, its owner is recorded on
    wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(wolf.wolf, exit)); });
    wolf.party.Until([&] { return wolf.guest.GetActorLatestAnimation(wolf.copy).EventName == "HorseExit"; }, "the copy played the action");
    CHECK(wolf.guest.SitState(wolf.copy) == ReplayState::kSitWantsToStand);
    wolf.party.Until([&] { return !streams.Held(wolf.copy) && streams.OnStream(wolf.copy); }, "then it is back on its owner's stream", [&] {
        return std::format("held {} sit state {} exit in {}", streams.Held(wolf.copy), wolf.guest.SitState(wolf.copy), wolf.guest.Actors.at(wolf.copy).ExitIn);
    });
    CHECK(wolf.guest.SitState(wolf.copy) == 0);
    CHECK_FALSE(wolf.guest.Mounts.contains(wolf.copy));
}

TEST_CASE("A streamed copy takes its owner's actor state with each action", "[animation][replay-start][players]")
{
    // Skyrim (duo, sneak-host-player, 2026-11-10): a streamed player's copy never sneaked. Sneaking is the actor's
    // state (flags1), toggled as the engine takes the sneak action; on its stream a copy takes no action, and the
    // flags an action carried were its owner's from before it. The action carries the state after it too.
    Party party(1, SmokeWorld, true);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    for (auto* engine : {&host, &guest})
    {
        engine->Graph = FakeGraphRules{};
        engine->GraphTransitions = {{{"Standing", "SneakStart"}, "Sneaking"}, {{"Sneaking", "SneakStop"}, "Standing"}};
        engine->ActionStates = {{"SneakStart", {1u << 9, true}}, {"SneakStop", {1u << 9, false}}};
        engine->Actors.at(FakeEngine::kPlayer).GraphState = "Standing";
    }
    uint32_t session = 0, copy = 0;
    party.As(party.Host(), [&] { session = Utils::GetSessionIdByFormId(FakeEngine::kPlayer).value_or(0); });
    party.Until([&] {
        party.As(party.Guest(), [&] { copy = Utils::GetFormIdBySessionId(session); });
        return copy && party.Guest().GetGraphStreams().OnStream(copy);
    }, "the host's player's copy is on its stream");
    constexpr uint32_t cSneaking = 1u << 9;
    const auto& cCopy = guest.Actors.at(copy);
    for (const bool cSneak : {true, false})
    {
        ActionEvent action;
        action.ActionId = cSneak ? 0x13000 : 0x13001;
        action.EventName = cSneak ? "SneakStart" : "SneakStop";
        party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(FakeEngine::kPlayer, action)); });
        REQUIRE(((host.Actors.at(FakeEngine::kPlayer).State1 & cSneaking) != 0) == cSneak); // the action toggled it
        party.Until([&] { return guest.GetActorLatestAnimation(copy).EventName == action.EventName; }, "its copy has the action");
        CHECK(party.Guest().GetGraphStreams().OnStream(copy));
        CHECK(((cCopy.State1 & cSneaking) != 0) == cSneak);
    }
}

TEST_CASE("A copy made after its owner sat down is seated, not about to sit as its owner's last action left it", "[animation][replay-start]")
{
    // Skyrim (duo, npc-clips-match, 2026-11-10): Lucan's copy stayed in sit state 1 (wants to sit) while its owner
    // leaned on his counter (3). The sit action leaves 1 and the furniture procedure sets 3 with no action (39912);
    // the spawn's graph state carries the 3, and a copy already on its stream dropped that state and took the
    // spawn action's.
    Party party(1, SmokeWorld, true);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    constexpr uint32_t cSitMask = 0xFu << FakeEngine::kSitShift;
    for (auto* engine : {&host, &guest})
    {
        engine->Graph = FakeGraphRules{};
        engine->GraphTransitions = {{{"Standing", "sitEnter"}, "Sitting"}};
        engine->ActionStates = {{"sitEnter", {1u << FakeEngine::kSitShift, true}}};
    }
    guest.NewCopiesGraphReady = false; // its stream starts before its spawn's state could load
    uint32_t npc = 0;
    party.As(party.Host(), [&] {
        npc = host.LoadNpc(0x13487, 0x31D2E, {});
        auto& owner = host.Actors.at(npc);
        owner.GraphState = "Standing";
        ActionEvent sit;
        sit.ActionId = 0x13031;
        sit.EventName = "sitEnter";
        REQUIRE(host.PerformAnimation(npc, sit));
        REQUIRE(host.SitState(npc) == 1);
        owner.State1 = (owner.State1 & ~cSitMask) | 3u << FakeEngine::kSitShift; // its procedure, once it sits
    });
    party.Until([&] { return guest.CopiesOf(0x13487).size() == 1; }, "the copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x13487).front();
    party.Until([&] { return party.Guest().GetGraphStreams().OnStream(cCopy); }, "the copy is on its owner's stream");
    const uint64_t cFrom = party.Guest().GetFrameTick();
    party.Until([&] { return party.Guest().GetFrameTick() > cFrom + 300; }, "300 ms on");
    CHECK(guest.SitState(cCopy) == 3);
    CHECK(guest.Actors.at(cCopy).GraphState == "Sitting");
}

TEST_CASE("A streamed owner's action carries no graph state, and its copy still follows it", "[animation][replay-start]")
{
    // Skyrim (duo, profile of ten wolves fighting, 2026-11-10): every accepted action carried its owner's whole graph
    // state (7-8 KB) for a copy's recovery, 70 KB/s of the fight's 85. A streamed owner's copies take no actions.
    Party party(1, SmokeWorld, true);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    for (auto* engine : {&host, &guest})
    {
        engine->Graph = FakeGraphRules{};
        engine->GraphTransitions = {{{"Standing", "sitEnter"}, "Sitting"}};
    }
    uint32_t npc = 0;
    party.As(party.Host(), [&] {
        npc = host.LoadNpc(0x13487, 0x31D2E, {});
        host.Actors.at(npc).GraphState = "Standing";
    });
    party.Until([&] { return guest.CopiesOf(0x13487).size() == 1; }, "the copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x13487).front();
    party.Until([&] { return party.Guest().GetGraphStreams().OnStream(cCopy); }, "the copy is on its owner's stream");
    party.As(party.Host(), [&] {
        REQUIRE(party.Host().GetGraphStreams().Streaming(npc));
        ActionEvent sit;
        sit.ActionId = 0x13031;
        sit.EventName = "sitEnter";
        REQUIRE(host.PerformAnimation(npc, sit));
    });
    CHECK(host.Actors.at(npc).LatestAnimation.GraphState.empty());
    party.Until([&] { return guest.Actors.at(cCopy).GraphState == "Sitting"; }, "the copy sits by its owner's stream");
}

TEST_CASE("A player's copy is back on its stream only once its owner has left its kill move", "[animation][replay-start][kill-move][players]")
{
    // Skyrim (duo, host-kill-move, 2026-11-10): the pair's instance ended at the victim's PairEnd, 1.3 s before the
    // attacker's KillMoveEnd. The attacker's copy took a sync point then, in the middle of its synchronized clip,
    // which started over: the whole kill move played a second time.
    WolfTurn wolf;
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->GraphTransitions[{"Standing", "KillMoveI"}] = "KillMove";
        engine->GraphTransitions[{"Standing", "pa_KillMoveI"}] = "KillMove";
        engine->GraphTransitions[{"KillMove", "KillMoveEnd"}] = "Standing";
    }
    uint32_t session = 0, copy = 0;
    wolf.party.As(wolf.party.Host(), [&] { session = Utils::GetSessionIdByFormId(FakeEngine::kPlayer).value_or(0); });
    wolf.party.As(wolf.party.Guest(), [&] { copy = Utils::GetFormIdBySessionId(session); });
    REQUIRE(copy != 0);
    // The fixture's players have no graph: both get one, as every actor in Skyrim has.
    wolf.host.Actors.at(FakeEngine::kPlayer).GraphState = "Standing";
    wolf.guest.Actors.at(copy).GraphState = "Standing";
    auto& streams = wolf.party.Guest().GetGraphStreams();
    const auto& cCopy = wolf.guest.Actors.at(copy);
    wolf.party.Until([&] { return streams.OnStream(copy) && SyncPoints(cCopy) > 0; }, "the host's player's copy is on its stream");
    ActionEvent kill;
    kill.ActionId = 0x13005;
    kill.EventName = "pa_KillMoveI";
    kill.TargetEventName = "KillMoveI";
    kill.TargetId = wolf.wolf;
    wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(FakeEngine::kPlayer, kill)); });
    wolf.party.Until([&] { return cCopy.GraphState == "KillMove" && streams.Held(copy); }, "its copy plays its half, off its stream");
    const size_t cPoints = SyncPoints(cCopy);
    // The instance ends with the victim's clip; the attacker's kill move runs on.
    auto& owner = wolf.host.Actors.at(FakeEngine::kPlayer);
    wolf.party.As(wolf.party.Host(), [&] {
        wolf.host.StopPairClip(wolf.wolf);
        wolf.host.StopPairClip(FakeEngine::kPlayer);
    });
    REQUIRE_FALSE(owner.InPair);
    REQUIRE(owner.InKillMove);
    const uint64_t cFrom = wolf.party.Guest().GetTick();
    wolf.party.Until([&] { return wolf.party.Guest().GetTick() >= cFrom + 1000; }, "the attacker's kill move runs on a second");
    CHECK(SyncPoints(cCopy) == cPoints);
    CHECK_FALSE(streams.OnStream(copy));
    wolf.party.As(wolf.party.Host(), [&] { wolf.host.EndKillMove(FakeEngine::kPlayer); });
    wolf.party.Until([&] { return streams.OnStream(copy) && SyncPoints(cCopy) > cPoints; }, "then its copy follows from a new sync point");
}

TEST_CASE("A copy's death comes at playback, however late its owner's last actions played on it", "[animation][replay-start][kill-move]")
{
    // Skyrim (duo, host-kill-move, 2026-11-09): off its stream for its owner's pair, the wolf's copy stood 330 ms after
    // its clip's KillActor. Its owner's actions at the pair's start had queued up on it (a step each, two graph
    // updates), the last 400 ms late, and its death was spaced from that one (ActionDue): 250 ms after playback.
    WolfTurn wolf;
    const std::array<std::string, 6> cEvents{"moveA", "moveB", "moveC", "moveD", "moveE", "moveF"};
    for (auto* engine : {&wolf.host, &wolf.guest})
    {
        engine->GraphTransitions[{"Standing", "KillMoveI"}] = "KillMove";
        for (const auto& cEvent : cEvents)
        {
            engine->GraphTransitions[{"KillMove", cEvent}] = "KillMove";
            engine->ClipFiles.insert({cEvent, "KillMove"});
        }
    }
    wolf.guest.ClipLoadFrames = 150; // each waits for its clip files on the copy's game
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    ActionEvent kill;
    kill.ActionId = 0x13005;
    kill.EventName = "pa_KillMoveI";
    kill.TargetEventName = "KillMoveI";
    kill.TargetId = wolf.wolf;
    wolf.party.As(wolf.party.Host(), [&] {
        REQUIRE(wolf.host.PerformAnimation(FakeEngine::kPlayer, kill));
        // Its owner's actions as the pair starts, all in one frame.
        for (size_t i = 0; i < cEvents.size(); ++i)
        {
            ActionEvent move;
            move.ActionId = 0x959F0 + uint32_t(i);
            move.EventName = cEvents[i];
            REQUIRE(wolf.host.PerformAnimation(wolf.wolf, move));
        }
    });
    wolf.party.Until([&] { return wolf.guest.GetActorLatestAnimation(wolf.copy).EventName == cEvents.back(); },
                     "the copy played its owner's actions, one step each");
    const uint64_t cFrom = wolf.party.Host().GetTick();
    wolf.party.Until([&] { return wolf.party.Host().GetTick() >= cFrom + 500; }, "the pair's clip runs");
    uint64_t died = 0;
    wolf.party.As(wolf.party.Host(), [&] {
        died = wolf.party.Host().GetTick();
        wolf.host.Strikes(FakeEngine::kPlayer, wolf.wolf, 1000.f);
    });
    REQUIRE(wolf.host.Actors.at(wolf.wolf).Dead);
    wolf.party.Until([&] { return cCopy.Dead; }, "the copy dies with its owner");
    uint64_t shown = 0;
    for (auto&& [entity, form, animation] : wolf.party.Guest().view<FormIdComponent, RemoteAnimationComponent>().each())
        if (form.Id == wolf.copy)
            shown = InterpolationSystem::PlaybackTick(wolf.party.Guest(), entity);
    INFO("the copy died showing its owner " << int64_t(shown) - int64_t(died) << " ms after the death");
    CHECK(shown < died + 100);
}

TEST_CASE("A copy back off its stream dies with its owner, not as long after an action from before the stream", "[animation][replay-start]")
{
    // Skyrim (duo, host-kill-move, 2026-11-09): the wolf's copy stood 330-350 ms after its blow. Its last replayed
    // action, 5 s before, still anchored its next (ActionDue): its graph's clock had run 110-150 ms short of its
    // owner's ticks on the stream, and its death waited that long. On its stream a copy has no anchor.
    WolfTurn wolf;
    auto& streams = wolf.party.Guest().GetGraphStreams();
    const auto& cCopy = wolf.guest.Actors.at(wolf.copy);
    // Off its stream (its own body in a kill move), it replays its owner's action: that action anchors its next.
    for (auto* engine : {&wolf.host, &wolf.guest})
        engine->GraphTransitions[{"Standing", "staggerStart"}] = "Standing";
    wolf.guest.Actors.at(wolf.copy).InKillMove = true;
    wolf.party.As(wolf.party.Guest(), [&] { streams.HoldWhile(wolf.copy, true); });
    wolf.party.Until([&] { return streams.Held(wolf.copy); }, "the copy is off its owner's stream on its own body");
    const size_t cPoints = SyncPoints(cCopy);
    ActionEvent stagger;
    stagger.ActionId = 0x13040;
    stagger.EventName = "staggerStart";
    wolf.party.As(wolf.party.Host(), [&] { REQUIRE(wolf.host.PerformAnimation(wolf.wolf, stagger)); });
    wolf.party.Until([&] { return cCopy.LatestAnimation.EventName == "staggerStart"; }, "the copy replays its owner's action");
    wolf.guest.Actors.at(wolf.copy).InKillMove = false;
    wolf.party.Until([&] { return streams.OnStream(wolf.copy) && SyncPoints(cCopy) > cPoints; }, "the copy is back on its stream");
    // Its graph's clock falls behind its owner's ticks while it is on the stream.
    const uint64_t cFrom = wolf.party.Guest().GetTick();
    wolf.guest.Actors.at(wolf.copy).GraphUpdating = false;
    wolf.party.Until([&] { return wolf.party.Guest().GetTick() >= cFrom + 200; }, "200 ms its graph does not step in");
    wolf.guest.Actors.at(wolf.copy).GraphUpdating = true;
    uint64_t died = 0;
    wolf.party.As(wolf.party.Host(), [&] {
        died = wolf.party.Host().GetTick();
        wolf.host.Strikes(FakeEngine::kPlayer, wolf.wolf, 1000.f);
    });
    REQUIRE(wolf.host.Actors.at(wolf.wolf).Dead);
    wolf.party.Until([&] { return cCopy.Dead; }, "the copy dies with its owner");
    uint64_t shown = 0;
    for (auto&& [entity, form, animation] : wolf.party.Guest().view<FormIdComponent, RemoteAnimationComponent>().each())
        if (form.Id == wolf.copy)
            shown = InterpolationSystem::PlaybackTick(wolf.party.Guest(), entity);
    INFO("the copy died showing its owner " << int64_t(shown) - int64_t(died) << " ms after the death");
    CHECK(shown < died + 100);
}

TEST_CASE("A copy starting after its owner's idle loads that idle's clip files, not its last event's", "[animation][replay-start]")
{
    // Skyrim (duo, Whiterun, 2026-11-09): copy 1A684 started on its owner's stream as its owner leaned on a counter. At
    // AnimObjectCounterRightToArm its clip had no file: its sync point named the owner's last graph event, which loads
    // nothing, while the owner's process held its idle's clip set (AIProcess 39625: one action set).
    Party party(1, SmokeWorld, true);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    for (auto* engine : {&host, &guest})
    {
        engine->Graph = FakeGraphRules{.Clips = {{.State = "CounterLoop", .Ms = 500, .Starts = {"CounterArm"}},
                                                 {.State = "CounterArm", .Ms = 800, .Starts = {"CounterLoop"}}}};
        engine->GraphTransitions = {{{"Standing", "IdleCounter"}, "CounterLoop"}, {{"CounterLoop", "IdleLook"}, "CounterLoop"}};
        engine->ClipFiles = {{"IdleCounter", "CounterArm"}};
        engine->SkippedClips = {{"CounterArm", "CounterLoop"}};
    }
    uint32_t npc = 0;
    party.As(party.Host(), [&] {
        npc = host.LoadNpc(0x1A684, 0x31D2E, {});
        host.Actors.at(npc).GraphState = "Standing";
        ActionEvent idle;
        idle.ActionId = 0x13004;
        idle.EventName = "IdleCounter";
        REQUIRE(host.PerformAnimation(npc, idle));
        ActionEvent look;
        look.ActionId = 0x13005;
        look.EventName = "IdleLook";
        REQUIRE(host.PerformAnimation(npc, look));
    });
    party.Until([&] { return guest.CopiesOf(0x1A684).size() == 1; }, "the copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x1A684).front();
    party.Until([&] { return party.Guest().GetGraphStreams().OnStream(cCopy) && SyncPoints(guest.Actors.at(cCopy)) >= 1; },
                "the copy starts on its owner's stream");
    const auto& cOwner = host.Actors.at(npc);
    party.Until([&] { return cOwner.GraphState == "CounterArm"; }, "the owner's loop reaches the arm clip");
    party.Until([&] { return cOwner.GraphState == "CounterLoop"; }, "and loops back");
    const uint64_t cFrom = party.Guest().GetFrameTick();
    party.Until([&] { return party.Guest().GetFrameTick() > cFrom + 500; }, "the copy plays it too");
    CHECK(guest.Actors.at(cCopy).LoadedClips.contains("IdleCounter"));
    CHECK(FirstGraphDifference(cOwner.Graph.History, guest.Actors.at(cCopy).Graph.History) == "");
}

TEST_CASE("A copy keeps its owner's idle's clip files through a turn its owner's process loaded nothing for", "[animation][replay-start]")
{
    // Skyrim (duo, Whiterun, 2026-11-09): copies asked for every event's clip files; a turn's (NPC_TurnToWalkLeft90,
    // loaded by its owner's movement, not its action set) took the place of the counter idle's set its owner still
    // held, and AnimObjectCounterRightToArm then played with no file.
    Party party(1, SmokeWorld, true);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    for (auto* engine : {&host, &guest})
    {
        engine->Graph = FakeGraphRules{.Clips = {{.State = "CounterLoop", .Ms = 500, .Starts = {"CounterArm"}},
                                                 {.State = "CounterArm", .Ms = 800, .Starts = {"CounterLoop"}}}};
        engine->GraphTransitions = {{{"Standing", "IdleCounter"}, "CounterLoop"}, {{"CounterLoop", "TurnToWalk"}, "CounterLoop"}};
        engine->ClipFiles = {{"IdleCounter", "CounterArm"}};
        engine->OtherSetClips = {"TurnToWalk"};
        engine->SkippedClips = {{"CounterArm", "CounterLoop"}};
    }
    uint32_t npc = 0;
    party.As(party.Host(), [&] {
        npc = host.LoadNpc(0x1A684, 0x31D2E, {});
        host.Actors.at(npc).GraphState = "Standing";
    });
    party.Until([&] { return guest.CopiesOf(0x1A684).size() == 1; }, "the copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x1A684).front();
    party.Until([&] { return party.Guest().GetGraphStreams().OnStream(cCopy); }, "the copy starts on its owner's stream");
    ActionEvent idle;
    idle.ActionId = 0x13004;
    idle.EventName = "IdleCounter";
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(npc, idle)); });
    party.Until([&] { return guest.Actors.at(cCopy).LoadedClips.contains("IdleCounter"); }, "the copy loads the idle's files as its owner did");
    ActionEvent turn;
    turn.ActionId = 0x13006;
    turn.EventName = "TurnToWalk";
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(npc, turn)); });
    const auto& cOwner = host.Actors.at(npc);
    party.Until([&] { return cOwner.GraphState == "CounterArm"; }, "the owner's loop reaches the arm clip");
    party.Until([&] { return cOwner.GraphState == "CounterLoop"; }, "and loops back");
    const uint64_t cFrom = party.Guest().GetFrameTick();
    party.Until([&] { return party.Guest().GetFrameTick() > cFrom + 500; }, "the copy plays it too");
    CHECK(guest.Actors.at(cCopy).LoadedClips.contains("IdleCounter"));
    CHECK(FirstGraphDifference(cOwner.Graph.History, guest.Actors.at(cCopy).Graph.History) == "");
}
