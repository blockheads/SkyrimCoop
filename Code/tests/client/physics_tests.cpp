// Physics sync's pure halves (docs/design/physics-sync.md): the guess every game makes, the host's send rule, the
// guest's playback.
#include <catch2/catch.hpp>
#include <Games/KnockReplay.h>
#include <Games/Animation/PoseMatch.h>
#include <Structs/AnimationGraphDescriptorManager.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_Master_Behavior.h>

#include "../FakeFlight.h"

#include <Physics/BodyPlayback.h>
#include <Physics/RagdollPending.h>
#include <Physics/BodyRelevance.h>
#include <Physics/BodySends.h>
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

Run Fly(FakeFlight aFlight, const BodySends::Limits& acLimits = {})
{
    BodySends sends(acLimits);
    BodyMotions motions;
    BodyPlayback playback;
    Run run;
    uint64_t tick = 1000;
    for (int step = 0; step < 600 && !aFlight.Resting; ++step, tick += kStepMs)
    {
        std::vector<BodyFrame> awake{aFlight.Body};
        motions.Mark(awake, tick);
        const auto cDue = sends.Due(awake, tick);
        run.Frames += cDue.size();
        playback.Add(tick, cDue);
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
        CHECK(cRun.Frames < 30);
    }
    // A bounce is seen a step after it: the guess went on into the floor for one step past the threshold.
    THEN("the guest is never further from the host's than a correction") CHECK(cRun.WorstError < 3.f * BodySends::Limits{}.Distance);
}

SCENARIO("A body the guest guesses right sends again only to repair a lost frame", "[physics]")
{
    BodySends sends;
    BodyFrame frame{.Key = {.Object = GameId(0, 1)}, .LinearVelocity = {100.f, 0.f, 0.f}};
    REQUIRE(sends.Due({frame}, 1000).size() == 1);
    for (uint64_t tick = 1016; tick < 1500; tick += 16)
    {
        frame.Position.x = 100.f * static_cast<float>(tick - 1000) / 1000.f;
        CHECK(sends.Due({frame}, tick).empty());
    }
    frame.Position.x = 50.f;
    CHECK(sends.Due({frame}, 1500).size() == 1);
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
    BodySends::Limits limits;
    limits.BurstBytes = 10 * BodyFrame::kWireBytes;
    BodySends sends(limits);
    std::vector<BodyFrame> shelf;
    for (uint8_t i = 0; i < 40; ++i)
        shelf.push_back({.Key = {.Object = GameId(0, 0x100u + i)}});
    // A burst at most, then the budget's share per update.
    CHECK(sends.Due(shelf, 1000).size() == 10);
    CHECK(sends.Due(shelf, 1000).empty());
    CHECK(sends.Due(shelf, 1030).size() == static_cast<size_t>(limits.BytesPerSecond * 0.03f / BodyFrame::kWireBytes));
    THEN("never more than a burst saved up") CHECK(sends.Due(shelf, 5000).size() == 10);

    WHEN("one body is far more wrong than another")
    {
        BodySends fresh;
        std::vector<BodyFrame> two{{.Key = {.Object = GameId(0, 1)}}, {.Key = {.Object = GameId(0, 2)}}};
        fresh.Due(two, 1000);
        two[0].Position.x = 3.f;
        two[1].Position.x = 30.f;
        const auto cDue = fresh.Due(two, 1016);
        REQUIRE(cDue.size() == 2);
        CHECK(cDue[0].Key == two[1].Key);
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
    THEN("it moves from its guess into the frame, and stands on it when it is due, no correction after")
    {
        CHECK(playback.At(1050)[0].Body.Position.x == Approx(5.f));
        CHECK(playback.At(1075)[0].Body.Position.x == Approx(4.f + (7.5f - 4.f) * 0.5f));
        CHECK(playback.At(1100)[0].Body.Position.x == Approx(4.f));
        CHECK(playback.At(1150)[0].Body.Position.x == Approx(4.f));
    }
    WHEN("its rest is known too")
    {
        playback.At(1050);
        playback.At(1100);
        playback.Rest(1200, {{.Key = cKey, .Position = {6.f, 0.f, 0.f}}});
        CHECK(playback.At(1150)[0].Body.Position.x == Approx(4.f));
        CHECK(playback.At(1175)[0].Body.Position.x == Approx(5.f));
        const auto cAtRest = playback.At(1200);
        REQUIRE(cAtRest.size() == 1);
        CHECK(cAtRest[0].Rest);
    }
    THEN("its velocity is blended along: a held body is moving where it shows")
    {
        CHECK(playback.At(1050)[0].Body.LinearVelocity.x == Approx(100.f));
        CHECK(playback.At(1075)[0].Body.LinearVelocity.x == Approx(50.f));
    }
}

SCENARIO("A guest's playback starts from where this game's own physics put the body", "[physics]")
{
    BodyPlayback playback;
    const BodyKey cKey{.Object = GameId(0, 7)};
    // This game's copy of the blast pushed it to x = 6 before the host's first frame (at x = 0, still).
    playback.StartFrom({.Key = cKey, .Position = {6.f, 0.f, 0.f}});
    playback.Add(1000, {{.Key = cKey}});
    CHECK(playback.At(1000)[0].Body.Position.x == Approx(6.f));
    CHECK(playback.At(1050)[0].Body.Position.x == Approx(3.f));
    CHECK(playback.At(1100)[0].Body.Position.x == Approx(0.f));
}

SCENARIO("A guest's playback blends a correction in and places a rest once", "[physics]")
{
    BodyPlayback playback;
    const BodyKey cKey{.Object = GameId(0, 7)};
    playback.Add(1000, {{.Key = cKey, .LinearVelocity = {100.f, 0.f, 0.f}}});
    CHECK(playback.At(999).empty());
    REQUIRE(playback.At(1100).size() == 1);
    CHECK(playback.At(1100)[0].Body.Position.x == Approx(10.f));

    // The host's body hit something at 1100: it stands at x = 4.
    playback.Add(1100, {{.Key = cKey, .Position = {4.f, 0.f, 0.f}}});
    THEN("right after, it shows where its old guess was")
    {
        CHECK(playback.At(1100)[0].Body.Position.x == Approx(10.f));
        CHECK(playback.At(1150)[0].Body.Position.x == Approx(7.f));
        CHECK(playback.At(1200)[0].Body.Position.x == Approx(4.f));
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

TEST_CASE("A guest gets the frames of the bodies near its character", "[physics]")
{
    const BodyFrame cNear{.Key = {.Object = GameId(0, 1)}, .Position = {BodyRelevance::kGuestRadius - 100.f, 0.f, 0.f}};
    const BodyFrame cFar{.Key = {.Object = GameId(0, 2)}, .Position = {BodyRelevance::kGuestRadius + 100.f, 0.f, 0.f}};
    const auto cKept = BodyRelevance::Near({cNear, cFar}, glm::vec3{});
    REQUIRE(cKept.size() == 1);
    CHECK(cKept[0].Key == cNear.Key);
    // No copy shown yet (joining, travelling): every frame, as before relevance.
    CHECK(BodyRelevance::Near({cNear, cFar}, std::nullopt).size() == 2);
}

TEST_CASE("Physics playback keeps millisecond precision at the network's epoch ticks", "[physics][epoch]")
{
    constexpr uint64_t cEpoch = 1793145055000;
    const BodyKey cKey{.Object = GameId(0, 7)};
    BodyPlayback playback;
    playback.Add(cEpoch, {{.Key = cKey, .LinearVelocity = {150.f, 0.f, 0.f}}});
    auto shown = playback.At(cEpoch + 400);
    REQUIRE(shown.size() == 1);
    CHECK(shown[0].Body.Position.x == Approx(60.f));

    // The next frame already here: predict it backwards, then blend toward its due time.
    playback.Add(cEpoch + 500, {{.Key = cKey, .Position = {75.f, 0.f, 0.f}, .LinearVelocity = {150.f, 0.f, 0.f}}});
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

TEST_CASE("A copy's first ragdoll frame is steered unless it is well off", "[body-prediction][kill-move]")
{
    CHECK(BodyPrediction::PlaceDistance(true) < BodyPrediction::PlaceDistance(false));
    CHECK(BodyPrediction::PlaceDistance(true) > 12.f); // a kill move's start, measured
}

TEST_CASE("A copy's ragdoll is steered to its owner's rest, put there, then left alone", "[physics][rest]")
{
    using namespace BodyPrediction;
    // Steered toward its rest for half a second, put exactly there with every other body in one step, then left.
    CHECK(StepToRest(0) == RestStep::kSteer);
    CHECK(StepToRest(kRestSettleSteps - 1) == RestStep::kSteer);
    CHECK(StepToRest(kRestSettleSteps) == RestStep::kPlace);
    CHECK(StepToRest(kRestSettleSteps + 1) == RestStep::kLeave);
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
    CHECK(cWorst(false) <= 2.6f);       // within the shake itself
    CHECK(cWorst(true) > 2.f * 2.6f);   // guessed onward: twice the shake and more
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
