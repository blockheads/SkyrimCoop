// Physics sync's pure halves (docs/design/physics-sync.md): the guess every game makes, the host's send rule, the
// guest's playback.
#include <catch2/catch.hpp>
#include <Games/KnockReplay.h>
#include <Games/Animation/PoseMatch.h>
#include <Structs/AnimationGraphDescriptorManager.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_Master_Behavior.h>

#include "../FakeFlight.h"

#include <Physics/BodyGroups.h>
#include <Games/ScriptMotion.h>
#include <Physics/BodyPlayback.h>
#include <Physics/RagdollPending.h>
#include <Physics/RagdollStart.h>
#include <Physics/BodyRelevance.h>
#include <Physics/BodyMotions.h>
#include <Physics/BodyStream.h>
#include <Physics/HavokStep.h>
#include <Systems/CopyEase.h>
#include <Systems/CopyStep.h>

#include <glm/geometric.hpp>

using namespace BodyPrediction;

TEST_CASE("A pending ragdoll bootstrap keeps the newest pose and stays a placement", "[physics][ragdoll-pending]")
{
    RagdollPending pending;
    BodyFrame frame{.Key = {.Actor = 7, .Index = 3}, .Position = {1.f, 2.f, 3.f}};
    pending.Put(0xFF000800, frame, true);
    frame.Position.x = 20.f;
    pending.Put(0xFF000800, frame, false);
    frame.Position.x = 30.f;
    pending.Put(0xFF000801, frame, false);
    const auto cFirst = pending.Take(0xFF000800);
    REQUIRE(cFirst.size() == 1);
    CHECK(cFirst[0].Frame.Position.x == 20.f);
    CHECK(cFirst[0].Place);
    CHECK(pending.Take(0xFF000800).empty());
    const auto cRemade = pending.Take(0xFF000801);
    REQUIRE(cRemade.size() == 1);
    CHECK(cRemade[0].Frame.Position.x == 30.f);
    CHECK_FALSE(cRemade[0].Place);
}

TEST_CASE("A humanoid copy receives the owner's get-up animation choice", "[physics][knock-replay]")
{
    const auto* cpDescriptor = AnimationGraphDescriptorManager::Get().GetDescriptor(AnimationGraphDescriptor_Master_Behavior::m_key);
    REQUIRE(cpDescriptor);
    CHECK(cpDescriptor->IsSynced(187)); // iGetUpType, set by Actor::BeginGetUp (40150)
}

namespace
{
constexpr uint64_t kStepMs = 16;

// A bucket thrown up and forward from a table, spinning.
FakeFlight Throw()
{
    FakeFlight flight;
    flight.Body.Key = {.Object = GameId(0, 0x1A0F3)};
    flight.Body.Position = {100.f, 200.f, 80.f};
    flight.Body.LinearVelocity = {250.f, 0.f, 300.f};
    flight.Body.AngularVelocity = {0.f, 6.f, 0.f};
    return flight;
}

// The host's sends of one flight and the guest's playback, step by step; aCheck sees the guest's pose and the host's.
struct Run
{
    size_t Frames{};
    float WorstError{};
    uint64_t RestedAt{};
};

Run Fly(FakeFlight aFlight, const BodyStream::Limits& acLimits = {})
{
    BodyStream sends(acLimits);
    BodyMotions motions;
    BodyPlayback playback;
    Run run;
    uint64_t tick = 1000;
    for (int step = 0; step < 600 && !aFlight.Resting; ++step, tick += kStepMs)
    {
        std::vector<BodyFrame> awake{aFlight.Body};
        motions.Mark(awake, tick);
        std::vector<BodyFrame> due;
        for (const BodyState& cState : sends.Due({BodyState{.Frame = awake[0], .AuthSeq = 1}}, tick))
            due.push_back(cState.Frame);
        run.Frames += due.size();
        playback.Add(tick, due);
        // The guest at the same tick (no delay here: the network's is the scenarios' to see).
        for (const auto& cPose : playback.At(tick))
            run.WorstError = std::max(run.WorstError, glm::distance(cPose.Body.Position, aFlight.Body.Position));
        aFlight.Step(kStepMs / 1000.f);
    }
    run.RestedAt = tick;
    return run;
}
} // namespace

TEST_CASE("Every game's guess flies a free fall's arc and spins on", "[physics]")
{
    BodyFrame frame;
    frame.Position = {0.f, 0.f, 100.f};
    frame.LinearVelocity = {100.f, 0.f, 200.f};
    frame.AngularVelocity = {0.f, 0.f, 3.14159265f};
    frame.Gravity = true;
    const BodyFrame cHalf = Predict(frame, 0.5f);
    CHECK(cHalf.Position.x == Approx(50.f));
    CHECK(cHalf.Position.z == Approx(100.f + 100.f + 0.5f * kGravity * 0.25f));
    CHECK(cHalf.LinearVelocity.z == Approx(200.f + kGravity * 0.5f));
    THEN("half a turn in a second") CHECK(Between(Predict(frame, 1.f), frame).Angle == Approx(3.14159265f).margin(1e-3));

    frame.Gravity = false;
    CHECK(Predict(frame, 0.5f).Position.z == Approx(200.f));
    THEN("a guess stops after kMaxSeconds") CHECK(Predict(frame, 5.f).Position == Predict(frame, kMaxSeconds).Position);
}

TEST_CASE("A steered body joins its frame's path from where it is, without overshooting", "[physics]")
{
    // A copy's ragdoll body 20 units off and a quarter turn away from the owner's, which slides and spins.
    BodyFrame target;
    target.Position = {0.f, 0.f, 50.f};
    target.LinearVelocity = {60.f, 0.f, 0.f};
    target.AngularVelocity = {0.f, 0.f, 1.f};
    glm::vec3 position{0.f, 20.f, 50.f};
    glm::quat rotation = glm::angleAxis(-1.5707963f, glm::vec3(0.f, 0.f, 1.f));

    // Havok's steps at 60 Hz, two of them on a slow frame: steered once a frame, moved by the velocities alone.
    float gap = glm::distance(position, target.Position);
    for (int frame = 0; frame < 30; ++frame)
    {
        const BodyFrame cDue = Predict(target, frame * 2.f / 60.f);
        const auto cSteer = Steer(position, rotation, cDue);
        for (int step = 0; step < 2; ++step)
        {
            position += cSteer.Linear / 60.f;
            if (const float cSpeed = glm::length(cSteer.Angular); cSpeed > 1e-6f)
                rotation = glm::normalize(glm::angleAxis(cSpeed / 60.f, cSteer.Angular / cSpeed) * rotation);
        }
        const BodyFrame cThen = Predict(target, (frame + 1) * 2.f / 60.f);
        const float cGap = glm::distance(position, cThen.Position);
        CHECK(cGap <= gap + 1e-3f);
        gap = cGap;
        if (frame == 29)
        {
            CHECK(cGap < 0.01f);
            CHECK(Between({.Position = position, .Rotation = rotation}, cThen).Angle < 1e-3f);
        }
    }
    THEN("on its path it moves as its frame") CHECK(Steer(target.Position, target.Rotation, target).Linear == target.LinearVelocity);
    THEN("steered over the update's time, it is on its path when the update ends")
    {
        const auto cSteer = Steer({0.f, 20.f, 50.f}, target.Rotation, target, 2.f / 60.f);
        CHECK(glm::distance(glm::vec3(0.f, 20.f, 50.f) + cSteer.Linear * (2.f / 60.f), Predict(target, 2.f / 60.f).Position) < 1e-3f);
    }
}

TEST_CASE("A copy up from its own ragdoll is eased onto its owner's place, never jumped there", "[physics]")
{
    // Its engine stood it 16 units off and a little turned from its owner (knockdown-smoke).
    const glm::vec3 cOwner{-747.f, 1501.f, 6957.f};
    const glm::vec3 cOwn{-755.f, 1514.7f, 6957.f};
    CopyEase ease;
    ease.Begin(cOwn - cOwner, 6.2f - 0.1f, 1000);
    glm::vec3 last = cOwn;
    for (uint64_t tick = 1000; tick <= 1000 + CopyEase::kMs; tick += 16)
    {
        glm::vec3 position = cOwner;
        float heading = 0.1f;
        ease.Apply(tick, position, heading);
        if (tick == 1000)
        {
            CHECK(glm::distance(position, cOwn) < 1e-3f);
            CHECK(heading == Approx(6.2f));
        }
        CHECK(glm::distance(position, last) < 1.f);
        last = position;
    }
    glm::vec3 position = cOwner;
    float heading = 0.1f;
    ease.Apply(1000 + CopyEase::kMs, position, heading);
    THEN("on its owner's place after kMs") CHECK(position == cOwner);
    THEN("turned the short way round") CHECK(heading == Approx(0.1f));
}

TEST_CASE("A body falls freely while gravity alone changes its speed", "[physics]")
{
    CHECK(FallsFreely({10.f, 0.f, 0.f}, {10.f, 0.f, kGravity * 0.016f}, 0.016f));
    CHECK_FALSE(FallsFreely({10.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, 0.016f));   // on the ground
    CHECK_FALSE(FallsFreely({10.f, 0.f, -200.f}, {8.f, 0.f, 100.f}, 0.016f)); // a bounce
}

SCENARIO("A thrown body sends a few frames, not one per step", "[physics]")
{
    const Run cRun = Fly(Throw());
    INFO("frames " << cRun.Frames << ", worst error " << cRun.WorstError);
    THEN("its arc needs none between the throw and the bounces")
    {
        CHECK(cRun.Frames > 2);
        CHECK(cRun.Frames < static_cast<size_t>((cRun.RestedAt - 1000) / 16) / 2);
    }
}

SCENARIO("A body the guest guesses right sends again only to repair a lost frame", "[physics]")
{
    BodyStream sends;
    // In free fall: its arc is every game's guess (on the ground it goes steadily: "A watcher's object stops...").
    const BodyFrame cStart{.Key = {.Object = GameId(0, 1)}, .Position = {0.f, 0.f, 5000.f}, .LinearVelocity = {100.f, 0.f, 0.f}, .Gravity = true};
    BodyState state{.Frame = cStart, .AuthSeq = 1};
    // New: kChangedRepeats updates running, so a lost first one is repaired at once.
    for (uint64_t tick = 1000; tick < 1000 + 16 * BodyTolerances::kChangedRepeats; tick += 16)
    {
        state.Frame = Predict(cStart, static_cast<float>(tick - 1000) / 1000.f);
        REQUIRE(sends.Due({state}, tick).size() == 1);
    }
    const uint64_t cLast = 1000 + 16 * (BodyTolerances::kChangedRepeats - 1);
    for (uint64_t tick = cLast + 16; tick < cLast + BodyTolerances::kRefreshMs; tick += 16)
    {
        state.Frame = Predict(cStart, static_cast<float>(tick - 1000) / 1000.f);
        CHECK(sends.Due({state}, tick).empty());
    }
    state.Frame = Predict(cStart, static_cast<float>(cLast + BodyTolerances::kRefreshMs - 1000) / 1000.f);
    CHECK(sends.Due({state}, cLast + BodyTolerances::kRefreshMs).size() == 1);
}

TEST_CASE("A woken body that never left where it woke is not moving", "[physics][woken-still]")
{
    // The Sleeping Giant's bar: Orgnar's animated body wakes the bottles' island 15-18 times a minute (measured).
    const BodyFrame cWoke{.Position = {-1154.f, 22.f, 78.f}};
    BodyFrame now = cWoke;
    now.LinearVelocity = {40.f, 0.f, 0.f}; // a contact's burst (a bottle tilted on the bar's edge, measured): going nowhere
    now.Position.x += 0.5f;
    CHECK_FALSE(BodyPrediction::LeftRest(now, cWoke));
    now.Position.x += 2.f; // a knock covers this within a frame
    CHECK(BodyPrediction::LeftRest(now, cWoke));
    BodyFrame turned = cWoke;
    turned.Rotation = glm::angleAxis(0.1f, glm::vec3(0.f, 0.f, 1.f));
    CHECK(BodyPrediction::LeftRest(turned, cWoke));
}

SCENARIO("A guest's budget sends the most wrong first and the rest later", "[physics]")
{
    const size_t cBytes = BodyState{}.WireBytes();
    BodyStream::Limits limits;
    limits.BurstBytes = 10.f * static_cast<float>(cBytes);
    BodyStream sends(limits);
    std::vector<BodyState> shelf;
    for (uint8_t i = 0; i < 40; ++i)
        shelf.push_back({.Frame = {.Key = {.Object = GameId(0, 0x100u + i)}}, .AuthSeq = 1});
    // A burst at most, then the budget's share per update.
    CHECK(sends.Due(shelf, 1000).size() == 10);
    CHECK(sends.Due(shelf, 1000).empty());
    CHECK(sends.Due(shelf, 1030).size() == static_cast<size_t>(limits.BytesPerSecond * 0.03f / static_cast<float>(cBytes)));
    THEN("never more than a burst saved up") CHECK(sends.Due(shelf, 5000).size() == 10);

    WHEN("one body is far more wrong than another")
    {
        BodyStream fresh;
        std::vector<BodyState> two{{.Frame = {.Key = {.Object = GameId(0, 1)}}, .AuthSeq = 1}, {.Frame = {.Key = {.Object = GameId(0, 2)}}, .AuthSeq = 1}};
        for (uint64_t tick = 1000; tick < 1000 + BodyTolerances::kChangedRepeats; ++tick)
            (void)fresh.Due(two, tick);
        two[0].Frame.Position.x = 3.f;
        two[1].Frame.Position.x = 30.f;
        const auto cDue = fresh.Due(two, 1100);
        REQUIRE(cDue.size() == 2);
        CHECK(cDue[0].Frame.Key == two[1].Frame.Key);
    }
}

SCENARIO("A guest's playback arrives at a frame it has before the frame is due", "[physics]")
{
    BodyPlayback playback;
    const BodyKey cKey{.Object = GameId(0, 7)};
    playback.Add(1000, {{.Key = cKey, .LinearVelocity = {100.f, 0.f, 0.f}}});
    CHECK(playback.At(1000)[0].Body.Position.x == Approx(0.f));
    // The host's body hit something at 1100, standing at x = 4: the guest, behind, has that frame at 1050.
    playback.Add(1100, {{.Key = cKey, .Position = {4.f, 0.f, 0.f}}});
    THEN("on the ground it is shown on the line between the two, never past the second, and stands on it when it is due")
    {
        CHECK(playback.At(1050)[0].Body.Position.x == Approx(2.f));
        CHECK(playback.At(1075)[0].Body.Position.x == Approx(3.f));
        CHECK(playback.At(1100)[0].Body.Position.x == Approx(4.f));
        CHECK(playback.At(1150)[0].Body.Position.x == Approx(4.f));
    }
    WHEN("its rest is known too")
    {
        playback.At(1050);
        playback.At(1100);
        playback.Rest(1200, {{.Key = cKey, .Position = {6.f, 0.f, 0.f}}});
        CHECK(playback.At(1150)[0].Body.Position.x == Approx(5.f));
        CHECK(playback.At(1175)[0].Body.Position.x == Approx(5.5f));
        const auto cAtRest = playback.At(1200);
        REQUIRE(cAtRest.size() == 1);
        CHECK(cAtRest[0].Rest);
    }
    THEN("its velocity is the line's")
    {
        CHECK(playback.At(1050)[0].Body.LinearVelocity.x == Approx(40.f));
        CHECK(playback.At(1075)[0].Body.LinearVelocity.x == Approx(40.f));
    }
}

SCENARIO("A playback takes a late frame as its target at once (the steer smooths it) and hands a rest over once", "[physics]")
{
    BodyPlayback playback;
    const BodyKey cKey{.Object = GameId(0, 7)};
    playback.Add(1000, {{.Key = cKey, .LinearVelocity = {100.f, 0.f, 0.f}}});
    CHECK(playback.At(999).empty());
    REQUIRE(playback.At(1100).size() == 1);
    CHECK(playback.At(1100)[0].Body.Position.x == Approx(10.f));

    // The host's body hit something at 1100: it stands at x = 4.
    playback.Add(1100, {{.Key = cKey, .Position = {4.f, 0.f, 0.f}}});
    THEN("its target is the late frame from then")
    {
        CHECK(playback.At(1100)[0].Body.Position.x == Approx(4.f));
        CHECK(playback.At(1150)[0].Body.Position.x == Approx(4.f));
    }
    WHEN("it rests")
    {
        playback.Rest(1300, {{.Key = cKey, .Position = {4.5f, 0.f, 0.f}}});
        playback.Add(1250, {{.Key = cKey, .Position = {99.f, 0.f, 0.f}}}); // late, older than the rest
        const auto cAtRest = playback.At(1300);
        REQUIRE(cAtRest.size() == 1);
        CHECK(cAtRest[0].Rest);
        CHECK(cAtRest[0].Body.Position.x == 4.5f);
        CHECK(cAtRest[0].Frames == 2);
        THEN("once") CHECK(playback.At(1316).empty());
        CHECK(playback.Count() == 0);
    }
    WHEN("the host sends nothing more and no rest")
    {
        CHECK(!playback.At(1100 + BodyPlayback::kLostMs).empty());
        CHECK(playback.TakeLost().empty());
        CHECK(playback.At(1101 + BodyPlayback::kLostMs).empty());
        CHECK(playback.Count() == 0);
        const auto cLost = playback.TakeLost();
        REQUIRE(cLost.size() == 1);
        CHECK(cLost[0].Key == cKey);
        CHECK(cLost[0].Frames == 2);
        CHECK(playback.TakeLost().empty());
    }
}

TEST_CASE("A guest gets the moving states of the bodies near its character", "[physics]")
{
    CHECK(BodyRelevance::IsNear({BodyRelevance::kGuestRadius - 100.f, 0.f, 0.f}, glm::vec3{}));
    CHECK_FALSE(BodyRelevance::IsNear({BodyRelevance::kGuestRadius + 100.f, 0.f, 0.f}, glm::vec3{}));
}

TEST_CASE("Physics playback keeps millisecond precision at the network's epoch ticks", "[physics][epoch]")
{
    constexpr uint64_t cEpoch = 1793145055000;
    const BodyKey cKey{.Object = GameId(0, 7)};
    BodyPlayback playback;
    playback.Add(cEpoch, {{.Key = cKey, .LinearVelocity = {150.f, 0.f, 0.f}, .Gravity = true}});
    auto shown = playback.At(cEpoch + 400);
    REQUIRE(shown.size() == 1);
    CHECK(shown[0].Body.Position.x == Approx(60.f));

    // The next frame already here: predict it backwards, then blend toward its due time.
    playback.Add(cEpoch + 500, {{.Key = cKey, .Position = {75.f, 0.f, 0.f}, .LinearVelocity = {150.f, 0.f, 0.f}, .Gravity = true}});
    shown = playback.At(cEpoch + 450);
    REQUIRE(shown.size() == 1);
    CHECK(shown[0].Body.Position.x == Approx(67.5f));
    shown = playback.At(cEpoch + 600);
    REQUIRE(shown.size() == 1);
    CHECK(shown[0].Body.Position.x == Approx(90.f));
}

TEST_CASE("A reliable ragdoll rest carries a living knockdown even when all moving frames were lost", "[physics][knock-rest]")
{
    BodyPlayback playback;
    const BodyKey cKey{.Actor = 37};
    playback.Rest(1000, {{.Key = cKey, .Position = {1.f, 2.f, 3.f}, .Knocked = true}});
    const auto cShown = playback.At(1000);
    REQUIRE(cShown.size() == 1);
    CHECK(cShown[0].Rest);
    CHECK(cShown[0].Body.Knocked);
}

TEST_CASE("A body rest rejects held frames that arrive after that rest was played", "[physics][release-barrier]")
{
    BodyPlayback playback;
    const BodyKey cKey{.Object = GameId(0, 7)};
    playback.Add(1000, {{.Key = cKey, .Position = {1.f, 0.f, 0.f}}});
    playback.Rest(1100, {{.Key = cKey, .Position = {2.f, 0.f, 0.f}}});
    REQUIRE(playback.At(1100).size() == 1);
    playback.Add(1050, {{.Key = cKey, .Position = {100.f, 0.f, 0.f}}});
    CHECK(playback.At(1200).empty());
    playback.Add(1250, {{.Key = cKey, .Position = {3.f, 0.f, 0.f}}});
    const auto cNew = playback.At(1250);
    REQUIRE(cNew.size() == 1);
    CHECK(cNew[0].Body.Position.x == Approx(3.f));
    playback.ForgetUntil(cKey, 1300);
    playback.Add(1290, {{.Key = cKey, .Position = {100.f, 0.f, 0.f}}});
    CHECK(playback.At(1400).empty());
}

TEST_CASE("A copy with a ragdoll finishes a get-up action with the native knock-state transition", "[physics][knock-replay]")
{
    CHECK(KnockReplay::AfterAction(1, true, true) == 5);
    CHECK(KnockReplay::AfterAction(3, true, true) == 5);
    CHECK(KnockReplay::AfterAction(0, false, true) == 0);
    CHECK(KnockReplay::AfterAction(0, true, true) == 0);
    CHECK(KnockReplay::AfterAction(1, true, false) == 1);
    CHECK(KnockReplay::AfterAction(6, true, true) == 6);
}

TEST_CASE("A streamed copy standing goes down at its owner's ragdoll action", "[physics][knock-replay][fall]")
{
    CHECK(KnockReplay::GoesDown(0, true));
    CHECK_FALSE(KnockReplay::GoesDown(0, false)); // another action
    CHECK_FALSE(KnockReplay::GoesDown(2, true));  // down already, by its owner's body frame
    CHECK_FALSE(KnockReplay::GoesDown(6, true));  // getting up: its own
}

TEST_CASE("A ragdoll's bodies are its physics' while it lies and while it gets up", "[physics][knock-replay][rising]")
{
    // Getting up (5 queued, 6 rising) 58291 only drives the simulated bodies toward the animation.
    CHECK_FALSE(KnockReplay::BodiesSimulated(0));
    for (uint8_t knock = 1; knock <= 6; ++knock)
        CHECK(KnockReplay::BodiesSimulated(knock));
    CHECK(KnockReplay::Lies(1));
    CHECK(KnockReplay::Lies(4));
    CHECK_FALSE(KnockReplay::Lies(0));
    CHECK_FALSE(KnockReplay::Lies(5)); // its late frames must not knock a copy that is up
    CHECK_FALSE(KnockReplay::Lies(6));
}

TEST_CASE("A streamed copy's own body stands up at its owner's get-up; its first match takes its child at once", "[physics][knock-replay][pick]")
{
    CHECK(KnockReplay::StandsUp(1, true));
    CHECK(KnockReplay::StandsUp(4, true));
    CHECK_FALSE(KnockReplay::StandsUp(1, false)); // another action
    CHECK_FALSE(KnockReplay::StandsUp(0, true));  // not lying
    CHECK_FALSE(KnockReplay::StandsUp(5, true));  // rising already
    // hkbPoseMatchingGenerator (59583): no match yet is -1.
    CHECK(PoseMatch::Snaps(-1));
    CHECK_FALSE(PoseMatch::Snaps(0));
    CHECK_FALSE(PoseMatch::Snaps(2));
}

TEST_CASE("Havok steps with the frame at any frame rate", "[physics][havok-step]")
{
    constexpr float cLongest = 1.f / 60.f;
    // S4: at 144 fps the step is the frame, not 1/60 (2.4 frames, which wakes resting clutter).
    CHECK(Physics::HavokStep(1.f / 144.f, cLongest, 5) == Approx(1.f / 144.f));
    CHECK(Physics::HavokStep(1.f / 60.f, cLongest, 5) == Approx(1.f / 60.f));
    // A long frame splits into equal steps no longer than 1/60, as far as the step count allows.
    CHECK(Physics::HavokStep(1.f / 25.f, cLongest, 5) == Approx(1.f / 75.f));
    CHECK(Physics::HavokStep(0.5f, cLongest, 5) == Approx(0.1f));
    CHECK(Physics::HavokStep(1.f / 25.f, cLongest, 1) == Approx(1.f / 25.f));
}

TEST_CASE("A copy near its owner's place is stepped there, far off placed, lying left to its ragdoll", "[physics][copy-step]")
{
    // guest-holds-into-npc: a copy only placed never pushes the bucket its player holds against it.
    const glm::vec3 cAt{100.f, 200.f, 50.f};
    CHECK(CopyStep::Choose(false, true, true, cAt, cAt) == CopyMove::Step);
    CHECK(CopyStep::Choose(false, true, true, cAt, cAt + glm::vec3(40.f, -40.f, 10.f)) == CopyMove::Step);
    CHECK(CopyStep::Choose(false, true, true, cAt, cAt + glm::vec3(0.f, 0.f, CopyStep::kSnap + 1.f)) == CopyMove::Place);
    CHECK(CopyStep::Choose(false, true, true, cAt, cAt + glm::vec3(50.f, 50.f, 0.f)) == CopyMove::Place);
    CHECK(CopyStep::Choose(true, true, true, cAt, cAt) == CopyMove::Leave);
}

TEST_CASE("A copy whose owner is off the ground is placed on its path, never stepped", "[physics][copy-step]")
{
    // copy-jumps-smoothly-*: a stepped copy stayed on the ground while its owner rose, then was placed past kSnap and
    // fell by its own gravity, twice a jump.
    const glm::vec3 cAt{100.f, 200.f, 50.f};
    CHECK(CopyStep::Choose(false, false, true, cAt, cAt + glm::vec3(0.f, 0.f, 5.f)) == CopyMove::Place);
    CHECK(CopyStep::Choose(false, false, true, cAt, cAt) == CopyMove::Place);
    CHECK(CopyStep::Choose(true, false, true, cAt, cAt) == CopyMove::Leave);
}

TEST_CASE("A copy whose owner's capsule collides with nothing is placed, never stepped", "[physics][copy-step]")
{
    const glm::vec3 cAt(100.f, 200.f, 300.f);
    CHECK(CopyStep::Choose(false, true, false, cAt, cAt + glm::vec3(1.f, 0.f, 0.f)) == CopyMove::Place);
    CHECK(CopyStep::Choose(true, true, false, cAt, cAt) == CopyMove::Leave);
}

TEST_CASE("A copy's step is in its own frame: forward along its heading, right across it", "[physics][copy-step]")
{
    // Skyrim's heading: 0 faces +y, pi/2 faces +x.
    const auto cNear = [](const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b) < 1e-4f; };
    CHECK(cNear(CopyStep::Local({0.f, 10.f, 0.f}, 0.f), {0.f, 10.f, 0.f}));
    CHECK(cNear(CopyStep::Local({10.f, 0.f, 0.f}, 0.f), {10.f, 0.f, 0.f}));
    CHECK(cNear(CopyStep::Local({10.f, 0.f, 2.f}, 1.57079633f), {0.f, 10.f, 2.f}));
    CHECK(cNear(CopyStep::Local({0.f, 10.f, 0.f}, 1.57079633f), {-10.f, 0.f, 0.f}));
}

TEST_CASE("A copy on its owner's place steps by nothing, as a still NPC: its step would wake its island", "[physics][copy-step]")
{
    // Havok's step lands a hair short (0.04 units measured): a copy standing still must not chase it.
    CHECK(CopyStep::Translation({0.04f, 0.f, 0.f}, 0.f) == glm::vec3(0.f));
    CHECK(CopyStep::Translation({0.f, 0.3f, -0.3f}, 1.f) == glm::vec3(0.f));
    // A slow walk's frame (about 80 units/s at 60 fps) still steps.
    CHECK(glm::length(CopyStep::Translation({0.f, 1.3f, 0.f}, 0.f) - glm::vec3(0.f, 1.3f, 0.f)) < 1e-4f);
    CHECK(glm::length(CopyStep::Translation({10.f, 0.f, 2.f}, 1.57079633f) - glm::vec3(0.f, 10.f, 2.f)) < 1e-4f);
}

TEST_CASE("A copy on its owner's place stands at its owner's height, not its controller's rest", "[physics][copy-step]")
{
    // WarehouseTraps' gas plate (2026-11-23): the guest at 272.6, its copy's controller resting at 272.07.
    CHECK(CopyStep::RestHeight({-1121.f, -2566.f, 272.07f}, {-1121.04f, -2566.f, 272.6f}) == 272.6f);
    CHECK(CopyStep::RestHeight({0.f, 0.f, 99.f}, {0.f, 0.f, 100.f}) == 100.f);
    // Walking (off its owner's place), or a ledge apart: its controller's.
    CHECK(CopyStep::RestHeight({0.f, 0.f, 99.f}, {3.f, 0.f, 100.f}) == 99.f);
    CHECK(CopyStep::RestHeight({0.f, 0.f, 80.f}, {0.f, 0.f, 100.f}) == 80.f);
}

TEST_CASE("A copy's ragdoll takes up its owner's frames whole, each body from where it was", "[body-prediction][ragdoll-start][kill-move]")
{
    const auto cBody = [](uint8_t aIndex, float aOff, bool aFirst) {
        RagdollStart::Body body{.Position = {100.f + aOff, 0.f, 0.f}, .First = aFirst};
        body.Frame.Key = {.Actor = 7, .Index = aIndex};
        body.Frame.Position = {100.f, 0.f, 0.f};
        body.Frame.LinearVelocity = {0.f, 10.f, 0.f};
        return body;
    };
    RagdollStart start;
    // HeadButt, 2026-11-23: limbs 38 and 46 off, the rest under 16. All steered, from where each is.
    auto plan = start.Next({cBody(0, 5.f, true), cBody(5, 46.f, true), cBody(7, -38.f, true)}, 1000);
    CHECK_FALSE(plan.Place);
    CHECK(plan.Targets[1].Position.x == Approx(146.f));
    CHECK(plan.Targets[1].LinearVelocity.x == Approx(-46.f / RagdollStart::kSeconds));
    plan = start.Next({cBody(0, 0.f, false), cBody(5, 23.f, false), cBody(7, -19.f, false)}, 1100);
    CHECK(plan.Targets[1].Position.x == Approx(123.f));
    plan = start.Next({cBody(5, 0.f, false)}, 1200);
    CHECK(plan.Targets[0].Position.x == Approx(100.f));
    CHECK(plan.Targets[0].LinearVelocity == glm::vec3(0.f, 10.f, 0.f));
    // One body far off: all put there, the near ones too.
    plan = start.Next({cBody(0, 1.f, false), cBody(5, 70.f, false)}, 5000);
    CHECK(plan.Place);
    CHECK(plan.Targets[0].Position.x == 100.f);
}

TEST_CASE("A copy's ragdoll turns from where it was onto its owner's frame", "[body-prediction][ragdoll-start]")
{
    RagdollStart start;
    RagdollStart::Body body{.Rotation = glm::angleAxis(1.f, glm::vec3(0.f, 0.f, 1.f)), .First = true};
    CHECK(glm::angle(start.Next({body}, 0).Targets[0].Rotation) == Approx(1.f));
    body.First = false;
    CHECK(glm::angle(start.Next({body}, 100).Targets[0].Rotation) == Approx(0.5f));
    CHECK(glm::angle(start.Next({body}, 200).Targets[0].Rotation) == Approx(0.f).margin(1e-3));
}

TEST_CASE("A getting-up limb's shake is not guessed onward between its frames", "[physics][rising][shake]")
{
    // knockdown-smoke (duo, 2026-11-11): a getting-up owner's hand went back and forth 4-6 units a frame; its copy's
    // 8-14. The owner's body alternates each physics step, a frame of it reaches the copy every fourth step.
    const BodyKey cKey{.Actor = 7, .Index = 4};
    const auto cWorst = [&](bool aWithVelocity) {
        BodyPlayback playback;
        float worst = 0.f;
        for (uint64_t tick = 1000; tick < 3000; tick += 16)
        {
            const bool cOut = (tick / 16) % 2 == 0;
            if ((tick / 16) % 4 == 0)
                playback.Add(tick, {{.Key = cKey, .Position = {cOut ? 5.f : 0.f, 0.f, 0.f},
                                     .LinearVelocity = {aWithVelocity ? (cOut ? 300.f : -300.f) : 0.f, 0.f, 0.f}}});
            // Played 200 ms behind, as a copy's ragdoll.
            for (const auto& cPose : playback.At(tick - 200))
                worst = std::max(worst, std::abs(cPose.Body.Position.x - 2.5f));
        }
        return worst;
    };
    INFO("with its velocity the shown body strays " << cWorst(true) << " units from the shake's middle, without " << cWorst(false));
    // Shown between the states it has, with or without their velocity: within the shake itself.
    CHECK(cWorst(false) <= 2.6f);
    CHECK(cWorst(true) <= 2.6f);
}

TEST_CASE("A body is at rest once it keeps one pose for a while, whatever Havok's island says", "[physics][still]")
{
    BodyPrediction::Still still;
    BodyFrame frame{.Position = {10.f, 0.f, 0.f}};
    CHECK_FALSE(still.Read(frame, 1000));
    frame.Position.x += 0.3f; // a creep under the limit
    CHECK_FALSE(still.Read(frame, 1200));
    CHECK(still.Read(frame, 1300));
    SECTION("moving again starts over")
    {
        frame.Position.x += 1.f;
        CHECK_FALSE(still.Read(frame, 1400));
        CHECK_FALSE(still.Read(frame, 1600));
        CHECK(still.Read(frame, 1700));
    }
    SECTION("turning counts as moving")
    {
        frame.Rotation = glm::angleAxis(0.05f, glm::vec3(0.f, 0.f, 1.f));
        CHECK_FALSE(still.Read(frame, 1400));
    }
}

TEST_CASE("A steered object's correction never overshoots, whatever the frame time", "[physics][steer]")
{
    // Review of #56: a fixed 50 ms correction integrated over a 100 ms frame flips the error; over 125 ms it grows.
    const auto cRun = [](const std::vector<float>& acFrames) {
        float error = 1.f, worst = 1.f, last = acFrames.front(); // the game's frame time, from the frame before
        for (const float cFrame : acFrames)
        {
            const float cSeconds = BodyPrediction::SteerSeconds(last);
            error *= 1.f - cFrame / cSeconds;
            worst = std::max(worst, std::abs(error));
            last = cFrame;
        }
        return std::pair(std::abs(error), worst);
    };
    for (const float cFrame : {0.016f, 0.033f, 0.1f, 0.125f, 0.25f})
    {
        INFO(cFrame);
        const auto [cEnd, cWorst] = cRun(std::vector<float>(30, cFrame));
        CHECK(cEnd < 0.05f);
        CHECK(cWorst <= 1.f);
    }
    // One hitch among short frames: a bounded kick, then back on the path.
    std::vector<float> hitch(30, 0.016f);
    hitch[10] = 0.125f;
    const auto [cEnd, cWorst] = cRun(hitch);
    CHECK(cEnd < 0.05f);
    CHECK(cWorst < 2.f);
}

TEST_CASE("A played body on the ground chases its target and no further; in free fall it joins the arc", "[physics]")
{
    BodyFrame frame{.Position = {100.f, 0.f, 0.f}, .Rotation = {1.f, 0.f, 0.f, 0.f}, .LinearVelocity = {300.f, 0.f, 0.f}};
    const float cFrame = 1.f / 60.f;
    // Behind its target: toward it, not carried on by the target's own speed (it ran past a body slowing down).
    CHECK(BodyPrediction::Follow({95.f, 0.f, 0.f}, frame.Rotation, frame, cFrame).Linear.x == Approx(5.f / BodyPrediction::kSteerSeconds));
    CHECK(BodyPrediction::Follow(frame.Position, frame.Rotation, frame, cFrame).Linear == glm::vec3(0.f));
    frame.Gravity = true;
    CHECK(BodyPrediction::Follow(frame.Position, frame.Rotation, frame, cFrame).Linear.x == Approx(300.f));
}

TEST_CASE("An island is put to sleep only when every body in it is dynamic and still", "[physics]")
{
    CHECK(BodyPrediction::StillToSleep(true, 0.f, 0.f));
    CHECK(BodyPrediction::StillToSleep(true, 1.9f, 0.05f));
    CHECK_FALSE(BodyPrediction::StillToSleep(false, 0.f, 0.f)); // keyframed
    CHECK_FALSE(BodyPrediction::StillToSleep(true, 40.f, 0.f)); // falling
    CHECK_FALSE(BodyPrediction::StillToSleep(true, 0.f, 1.f));  // rolling in place
    // Sliding at a steady 90 units/s passes Havok's own last test against a fastest speed that high (0x140B869C0):
    // a free asleep beside it (FreeBody) asks this first (physics.md, "When an island sleeps").
    CHECK_FALSE(BodyPrediction::StillToSleep(true, 90.f, 0.f));
}

TEST_CASE("A rest queued for a reference's body is known before it shows", "[physics]")
{
    BodyPlayback playback;
    const BodyKey cKey{.Object = GameId(0, 0x1234)};
    CHECK_FALSE(playback.RestComing(cKey.Object));
    playback.Rest(1000, {BodyPose{.Key = cKey, .Position = {1.f, 2.f, 3.f}}});
    CHECK(playback.RestComing(cKey.Object));
    CHECK_FALSE(playback.RestComing(GameId(0, 0x9999)));
    // Shown: the row may place it again from here on.
    CHECK(playback.At(1000).size() == 1);
    CHECK_FALSE(playback.RestComing(cKey.Object));
}

TEST_CASE("What a body meets: a played one passes loose objects nobody moves, and other played ones", "[physics]")
{
    using BodyGroups::Decide;
    using BodyGroups::Meet;
    const auto cInfo = [](uint32_t aLayer, uint32_t aGroup) { return aLayer | (aGroup << 16); };
    const uint32_t cPlayed = cInfo(4, BodyGroups::kPlayed), cMoving = cInfo(4, BodyGroups::kMovingFirst), cResting = cInfo(4, 0);
    CHECK(Decide(cPlayed, cResting) == Meet::kPass);
    CHECK(Decide(cResting, cPlayed) == Meet::kPass);
    CHECK(Decide(cPlayed, cPlayed) == Meet::kPass);
    CHECK(Decide(cPlayed, cMoving) == Meet::kCollide);
    CHECK(Decide(cMoving, cInfo(4, BodyGroups::kMovingFirst + 1)) == Meet::kCollide);
    // One moving group: bodies that shared a group (a rope's links), kept apart as the engine kept them. The duo,
    // 2026-11-24: a tripwire's rope a guest took spun at 30 rad/s for good, its links colliding with each other.
    CHECK(Decide(cMoving, cMoving) == Meet::kEngine);
    BodyGroups::MovingGroups groups;
    const uint16_t cRope = groups.Take(12);
    CHECK(BodyGroups::IsMoving(cRope));
    CHECK(groups.Take(12) == cRope);   // its next link
    CHECK(groups.Take(13) != cRope);   // another object
    CHECK(groups.Take(0) != groups.Take(0)); // no group of its own: nothing shared
    CHECK(Decide(cMoving, cResting) == Meet::kEngine);
    CHECK(Decide(cResting, cResting) == Meet::kEngine);
    // The world (static, layer 1) and a character's capsule (layer 30, its own group) are the engine's to say.
    CHECK(Decide(cPlayed, cInfo(1, 0)) == Meet::kEngine);
    CHECK(Decide(cPlayed, cInfo(30, 7)) == Meet::kEngine);
}

TEST_CASE("A moving body's group waits for a played body: nothing else meets it differently", "[physics]")
{
    // The duo, 2026-11-22 (drop_wheel_on_wheels): a wheel the guest's standing player leaned on was claimed and rested
    // every 0.7 s, its group written each time (77794 redoes its agents), and slid 8 units in 8 s; vanilla's stays.
    using BodyGroups::Decide;
    using BodyGroups::Meet;
    const auto cInfo = [](uint32_t aLayer, uint32_t aGroup) { return aLayer | (aGroup << 16); };
    // Against anything not played the moving group only hands the pair to the engine, as its own group would.
    for (const uint32_t cOther : {cInfo(4, 0), cInfo(4, 12), cInfo(1, 0), cInfo(30, 7), cInfo(32, 9)})
    {
        CHECK(Decide(cInfo(4, BodyGroups::kMovingFirst), cOther) == Meet::kEngine);
        CHECK(Decide(cInfo(4, 12), cOther) == Meet::kEngine);
    }
    // Against a played one it is what makes them collide.
    CHECK(Decide(cInfo(4, BodyGroups::kMovingFirst), cInfo(4, BodyGroups::kPlayed)) == Meet::kCollide);
    CHECK(Decide(cInfo(4, 12), cInfo(4, BodyGroups::kPlayed)) == Meet::kPass);
    CHECK_FALSE(BodyGroups::WritesMoving(false));
    CHECK(BodyGroups::WritesMoving(true));
}

TEST_CASE("A lying copy's ragdoll, played, meets the world and what this game moves, not what its owner's settled", "[physics][played-ragdoll]")
{
    using BodyGroups::Decide;
    using BodyGroups::Meet;
    // A lying ragdoll's bodies are on layer 32 (L_DEADBIP: KillImpl, KnockExplosion 39895), which meets clutter, props,
    // debris and another actor's layer 32 (Skyrim.esm's COLL; docs/re/systems/physics.md, "A lying ragdoll's layer").
    const auto cInfo = [](uint32_t aLayer, uint32_t aGroup) { return aLayer | (aGroup << 16); };
    const uint32_t cCopy = cInfo(32, BodyGroups::kPlayed);
    CHECK(Decide(cCopy, cInfo(4, 0)) == Meet::kPass);                     // a ladle nobody moves
    CHECK(Decide(cCopy, cInfo(10, 0)) == Meet::kPass);                    // a prop nobody moves
    CHECK(Decide(cCopy, cInfo(4, BodyGroups::kPlayed)) == Meet::kPass);   // an object another game moves
    CHECK(Decide(cCopy, cInfo(32, BodyGroups::kPlayed)) == Meet::kPass);  // another copy's ragdoll
    CHECK(Decide(cCopy, cInfo(4, BodyGroups::kMovingFirst)) == Meet::kCollide); // what this game's player carries
    CHECK(Decide(cCopy, cInfo(1, 0)) == Meet::kEngine);                   // the floor
    CHECK(Decide(cCopy, cInfo(13, 0)) == Meet::kEngine);                  // terrain
    CHECK(Decide(cCopy, cInfo(32, 9)) == Meet::kEngine);                  // this game's own corpse: the layers say
}

TEST_CASE("A script's motion type packs with its allowActivate and the last one stands", "[physics][motion]")
{
    CHECK(ScriptMotion::TypeOf(ScriptMotion::Pack(4, true)) == 4);
    CHECK(ScriptMotion::AllowsActivate(ScriptMotion::Pack(4, true)));
    CHECK_FALSE(ScriptMotion::AllowsActivate(ScriptMotion::Pack(1, false)));
    ScriptMotion::Store store;
    CHECK_FALSE(store.Of(0x7998B));
    store.Remember(0x7998B, ScriptMotion::Pack(4, false));
    store.Remember(0x7998B, ScriptMotion::Pack(1, false)); // tripped
    CHECK(store.Of(0x7998B) == ScriptMotion::Pack(1, false));
    // Live, once for the call (the duo: a falling rock's row went out 57 times, re-applied each time on the guest).
    CHECK(store.TakeUnsent(0x7998B) == ScriptMotion::Pack(1, false));
    CHECK_FALSE(store.TakeUnsent(0x7998B));
    CHECK(store.Of(0x7998B) == ScriptMotion::Pack(1, false)); // a cell's state still has it
}

TEST_CASE("Moving groups never wrap onto one in use; a freed one is taken again", "[physics]")
{
    // Review of #129: past the range's 4076 values one was handed out again while still live, and two objects passed
    // through each other.
    BodyGroups::MovingGroups groups;
    const uint16_t cFirst = groups.Take(12);
    for (uint16_t i = 0; i < BodyGroups::kMovingLast - BodyGroups::kMovingFirst; ++i)
        (void)groups.Take(0);
    CHECK(groups.InUse() == size_t(BodyGroups::kMovingLast - BodyGroups::kMovingFirst + 1));
    CHECK(groups.Take(0) == 0);       // all in use: no moving group, never cFirst again
    CHECK(groups.Take(12) == cFirst); // its own group's still
    groups.Give(cFirst);
    groups.Give(cFirst);              // both of group 12's bodies stopped
    CHECK(groups.Take(13) == cFirst); // free again, taken by another object
}
