// Physics sync's pure halves (docs/design/physics-sync.md): the guess every game makes, the host's send rule, the
// guest's playback.
#include <catch2/catch.hpp>

#include "../FakeFlight.h"

#include <Physics/BodyPlayback.h>
#include <Physics/BodyRelevance.h>
#include <Physics/BodySends.h>

using namespace BodyPrediction;

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
