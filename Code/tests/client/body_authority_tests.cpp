// Fiedler's authority scheme's pure halves (docs/design/physics-authority-plan.md): who simulates each body, the
// stream to each game, and putting another game's rest on a body.
#include <catch2/catch.hpp>

#include <Physics/BodyAuthority.h>
#include <Physics/BodyStream.h>
#include <Physics/PairedPoses.h>
#include <Physics/RestPlacer.h>
#include <Wire/Buffer.h>

namespace
{
constexpr uint32_t kHost = 1, kGuest = 2, kOther = 3;
const BodyKey kBucket{.Object = {0, 0x12FDF}};
const BodyKey kPlate{.Object = {0, 0x12FE0}};
const BodyKey kCup{.Object = {0, 0x12FE1}};

BodyState StateOf(const BodyAuthority& acAuthority, const BodyKey& acKey, uint32_t aReceiver, const glm::vec3& acAt = {})
{
    return acAuthority.Stamp({.Key = acKey, .Position = acAt}, aReceiver);
}
} // namespace

TEST_CASE("Sequence numbers wrap: a number just past the top is newer", "[physics][authority]")
{
    CHECK(SeqAfter(1, 0));
    CHECK_FALSE(SeqAfter(0, 0));
    CHECK_FALSE(SeqAfter(0, 1));
    CHECK(SeqAfter(2, 65535));
    CHECK_FALSE(SeqAfter(65535, 2));
}

TEST_CASE("A body's state crosses the wire: moving quantized, at rest exact", "[physics][authority]")
{
    const BodyState cMoving{.Frame = {.Key = kBucket, .Position = {10.3f, -4.2f, 100.f}, .LinearVelocity = {50.f, 0.f, -20.f}, .Gravity = true},
                            .Author = BodyAuthor::kOther, .AuthSeq = 65535, .HoldSeq = 7};
    const BodyState cRest{.Frame = {.Key = kPlate, .Position = {10.123456f, -4.2f, 100.f}}, .Author = BodyAuthor::kYou, .AuthSeq = 3, .AtRest = true};
    for (const BodyState& cState : {cMoving, cRest})
    {
        Wire::Buffer buffer(256);
        Wire::Buffer::Writer writer(&buffer);
        cState.Serialize(writer);
        Wire::Buffer::Reader reader(&buffer);
        BodyState read;
        read.Deserialize(reader);
        CHECK(read == cState.Quantized());
    }
    CHECK(cRest.Quantized().Frame.Position.x == 10.123456f);
}

TEST_CASE("A player's touch takes a body; a held one is not taken", "[physics][authority]")
{
    BodyAuthority authority;
    CHECK(authority.Of(kBucket).Player == BodyAuthority::kNobody);
    REQUIRE(authority.Touch(kBucket, kGuest, 100));
    CHECK(authority.Of(kBucket) == BodyAuthority::Claim{kGuest, 1, 0, false, false});
    // Touching on is the same claim.
    REQUIRE(authority.Touch(kBucket, kGuest, 110));
    CHECK(authority.Of(kBucket).AuthSeq == 1);
    // The last toucher wins.
    REQUIRE(authority.Touch(kBucket, kOther, 120));
    CHECK(authority.Of(kBucket) == BodyAuthority::Claim{kOther, 2, 0, false, false});

    REQUIRE(authority.Grab(kPlate, kGuest, 100));
    CHECK(authority.Of(kPlate) == BodyAuthority::Claim{kGuest, 0, 1, true, false});
    CHECK_FALSE(authority.Touch(kPlate, kOther, 110));
    CHECK_FALSE(authority.Grab(kPlate, kOther, 110));
    authority.Release(kPlate, kGuest);
    CHECK(authority.Of(kPlate) == BodyAuthority::Claim{kGuest, 0, 1, false, false});
    CHECK(authority.Grab(kPlate, kOther, 120));
}

TEST_CASE("A body under a player takes what it touches, only if nobody has it, recursively", "[physics][authority]")
{
    BodyAuthority authority;
    authority.Touch(kBucket, kGuest, 100);
    authority.Touch(kCup, kOther, 100);
    const std::vector<std::pair<BodyKey, BodyKey>> cContacts{{kPlate, kBucket}, {kPlate, kCup}};
    const auto cTaken = authority.Spread(kGuest, cContacts, 110);
    REQUIRE(cTaken == std::vector<BodyKey>{kPlate});
    CHECK(authority.Of(kPlate) == BodyAuthority::Claim{kGuest, 1, 0, false, false});
    // Another player's moving body is not taken by contact.
    CHECK(authority.Is(kCup, kOther));

    // A chain: the plate takes the cup once nobody has the cup.
    BodyAuthority chain;
    chain.Touch(kBucket, kGuest, 100);
    CHECK(chain.Spread(kGuest, {{kBucket, kPlate}, {kPlate, kCup}}, 110).size() == 2);
    CHECK(chain.Is(kCup, kGuest));
    // A resting body of the player takes nothing.
    BodyAuthority resting;
    resting.Touch(kBucket, kGuest, 100);
    resting.Rest(kGuest, {kBucket}, {}, 1000);
    CHECK(resting.Spread(kGuest, {{kBucket, kPlate}}, 1100).empty());
}

TEST_CASE("A player's pile rests together, once every body in it is still and nobody leans on it", "[physics][authority]")
{
    BodyAuthority authority;
    authority.Touch(kBucket, kGuest, 0);
    authority.Spread(kGuest, {{kBucket, kPlate}}, 0);
    authority.Touch(kCup, kGuest, 0);
    const std::vector<std::pair<BodyKey, BodyKey>> cContacts{{kBucket, kPlate}};
    // The plate still moves: the bucket waits; the cup lies apart and rests (touched long enough ago).
    auto rested = authority.Rest(kGuest, {kBucket, kCup}, cContacts, 600);
    CHECK(rested == std::vector<BodyKey>{kCup});
    CHECK(authority.Of(kCup) == BodyAuthority::Claim{kGuest, 2, 0, false, true});
    CHECK_FALSE(authority.Of(kBucket).AtRest);
    // Both still: the pile rests as one.
    rested = authority.Rest(kGuest, {kBucket, kPlate}, cContacts, 700);
    CHECK(rested.size() == 2);
    CHECK(authority.Of(kBucket).AtRest);
    CHECK(authority.Of(kPlate).AtRest);
}

TEST_CASE("A pile the player's capsule leans on waits, at most kRestWaitMs", "[physics][authority]")
{
    BodyAuthority authority;
    authority.Touch(kBucket, kGuest, 0);
    uint64_t tick = 0;
    for (; tick < BodyTolerances::kRestWaitMs; tick += 100)
    {
        authority.Touch(kBucket, kGuest, tick); // the capsule pushes it every step, it does not move
        REQUIRE(authority.Rest(kGuest, {kBucket}, {}, tick).empty());
    }
    authority.Touch(kBucket, kGuest, tick);
    CHECK(authority.Rest(kGuest, {kBucket}, {}, tick + 100).size() == 1);
}

TEST_CASE("The host keeps the first claim it hears; the other guest yields to the host's state", "[physics][authority]")
{
    BodyAuthority host, guest, other;
    // Both guests touch the bucket at once, each from the same numbers.
    guest.Touch(kBucket, kGuest, 100);
    other.Touch(kBucket, kOther, 100);
    REQUIRE(host.Accept(kGuest, StateOf(guest, kBucket, kGuest)));
    CHECK_FALSE(host.Accept(kOther, StateOf(other, kBucket, kOther)));
    CHECK(host.Is(kBucket, kGuest));
    // The winner's later states stand; the loser hears the same numbers under another name and yields.
    CHECK(host.Accept(kGuest, StateOf(guest, kBucket, kGuest)));
    CHECK(other.Hear(StateOf(host, kBucket, kOther), kOther) == BodyAuthority::Heard::kApply);
    CHECK_FALSE(other.Is(kBucket, kOther));
    CHECK(guest.Hear(StateOf(host, kBucket, kGuest), kGuest) == BodyAuthority::Heard::kOld);
    CHECK(guest.Is(kBucket, kGuest));
    // A later touch by the loser takes it, by a newer number.
    other.Touch(kBucket, kOther, 200);
    CHECK(host.Accept(kOther, StateOf(other, kBucket, kOther)));
    CHECK(guest.Hear(StateOf(host, kBucket, kGuest), kGuest) == BodyAuthority::Heard::kApply);
}

TEST_CASE("A held body is its holder's alone at the host, whatever numbers another sends", "[physics][authority]")
{
    BodyAuthority host, guest, other;
    guest.Grab(kBucket, kGuest, 100);
    REQUIRE(host.Accept(kGuest, StateOf(guest, kBucket, kGuest)));
    for (int i = 0; i < 3; ++i)
        other.Touch(kBucket, kOther, 100 + i), other.Rest(kOther, {kBucket}, {}, 10000 + i);
    other.Touch(kBucket, kOther, 20000);
    CHECK_FALSE(host.Accept(kOther, StateOf(other, kBucket, kOther)));
    // The holder lets go: still its until it rests.
    guest.Release(kBucket, kGuest);
    CHECK(host.Accept(kGuest, StateOf(guest, kBucket, kGuest)));
    CHECK_FALSE(host.Of(kBucket).Held);
    CHECK(host.Is(kBucket, kGuest));
}

TEST_CASE("A guest's rest is its own until the host confirms it, then nobody's", "[physics][authority]")
{
    BodyAuthority host, guest;
    guest.Touch(kBucket, kGuest, 0);
    REQUIRE(host.Accept(kGuest, StateOf(guest, kBucket, kGuest)));
    // The host's stream from before the rest: older, ignored.
    const BodyState cBefore = StateOf(host, kBucket, kGuest);
    REQUIRE(guest.Rest(kGuest, {kBucket}, {}, 1000).size() == 1);
    CHECK(guest.Hear(cBefore, kGuest) == BodyAuthority::Heard::kOld);
    CHECK(guest.Is(kBucket, kGuest));
    // The rest reaches the host: nobody's there; its stream back confirms it here.
    REQUIRE(host.Accept(kGuest, StateOf(guest, kBucket, kGuest, {1.f, 2.f, 3.f})));
    CHECK(host.Is(kBucket, BodyAuthority::kNobody));
    CHECK(guest.Hear(StateOf(host, kBucket, kGuest), kGuest) == BodyAuthority::Heard::kConfirmed);
    CHECK(guest.Is(kBucket, BodyAuthority::kNobody));
    // From then on the host's states of it are shown.
    CHECK(guest.Hear(StateOf(host, kBucket, kGuest), kGuest) == BodyAuthority::Heard::kApply);
}

TEST_CASE("The host's game placing a body takes it from its mover by a newer number", "[physics][authority]")
{
    BodyAuthority host, guest;
    guest.Touch(kBucket, kGuest, 0);
    host.Accept(kGuest, StateOf(guest, kBucket, kGuest));
    host.Unclaim(kBucket);
    // The mover's state from before it heard: refused.
    CHECK_FALSE(host.Accept(kGuest, StateOf(guest, kBucket, kGuest)));
    CHECK(guest.Hear(StateOf(host, kBucket, kGuest), kGuest) == BodyAuthority::Heard::kApply);
    CHECK(guest.Is(kBucket, BodyAuthority::kNobody));
}

TEST_CASE("The stream sends a changed claim first, three updates running, then on error", "[physics][stream]")
{
    BodyStream stream;
    BodyState state{.Frame = {.Key = kBucket, .LinearVelocity = {10.f, 0.f, 0.f}}, .Author = BodyAuthor::kOther, .AuthSeq = 1};
    for (uint64_t tick = 10; tick <= 30; tick += 10)
        CHECK(stream.Due({state}, tick).size() == 1);
    // Moving on as guessed: nothing until the refresh.
    state.Frame.Position.x = 0.4f;
    CHECK(stream.Due({state}, 70).empty());
    // Off the guess past the limits: sent.
    state.Frame.Position.x = 10.f;
    CHECK(stream.Due({state}, 80).size() == 1);
    // At rest: a change, three times, then once a refresh.
    state.AtRest = true;
    state.Frame.LinearVelocity = {};
    for (uint64_t tick = 90; tick <= 110; tick += 10)
        CHECK(stream.Due({state}, tick).size() == 1);
    CHECK(stream.Due({state}, 1000).empty());
    CHECK(stream.Due({state}, 110 + BodyTolerances::kRestRefreshMs).size() == 1);
}

TEST_CASE("The stream keeps to the budget, the most wrong first", "[physics][stream]")
{
    BodyStream stream({.BytesPerSecond = 0.f, .BurstBytes = 80.f});
    std::vector<BodyState> states;
    for (uint32_t i = 0; i < 4; ++i)
        states.push_back({.Frame = {.Key = {.Object = {0, 0x100 + i}}}, .AuthSeq = 1});
    CHECK(stream.Due(states, 10).size() == 2);
}

TEST_CASE("A rest is glided onto, put there awake for one step, then slept with its island", "[physics][rest-placer]")
{
    using Step = RestPlacer::Step;
    RestPlacer placer(0);
    CHECK(placer.Next(false, false, false, false, false, 0) == Step::kGlide);
    CHECK(placer.Next(false, false, false, false, false, 400) == Step::kGlide);
    CHECK(placer.Next(false, false, false, false, false, 500) == Step::kPlace);
    // The step after: there, so slept; never in the same update as the place.
    CHECK(placer.Next(true, false, true, false, false, 516) == Step::kSleep);
    placer.Slept(true, 516);
    CHECK(placer.Done);
    CHECK(placer.Next(true, false, true, true, false, 600) == Step::kLeave);
}

TEST_CASE("A rest far off is put at once; one there asleep only sleeps (nothing moves it)", "[physics][rest-placer]")
{
    using Step = RestPlacer::Step;
    CHECK(RestPlacer(0).Next(false, true, false, false, false, 0) == Step::kPlace);
    RestPlacer there(0);
    CHECK(there.Next(true, false, true, true, false, 0) == Step::kSleep);
    there.Slept(true, 0);
    CHECK(there.Done);
    THEN("a reference near but awake is put exactly; a ragdoll near and still sleeps where it is")
    {
        CHECK(RestPlacer(0).Next(true, false, true, false, false, 0) == Step::kPlace);
        CHECK(RestPlacer(0, false).Next(true, false, true, false, false, 0) == Step::kSleep);
    }
}

TEST_CASE("A reference that leaves its rest in the place's step is held there; a ragdoll is slept", "[physics][rest-placer]")
{
    using Step = RestPlacer::Step;
    RestPlacer reference(0);
    REQUIRE(reference.Next(false, true, false, false, false, 0) == Step::kPlace);
    CHECK(reference.Next(false, false, false, false, false, 16) == Step::kHold);
    RestPlacer ragdoll(0, false);
    REQUIRE(ragdoll.Next(false, true, false, false, false, 0) == Step::kPlace);
    CHECK(ragdoll.Next(false, false, false, false, false, 16) == Step::kSleep);
}

TEST_CASE("A rest whose island will not sleep is left after kSleepMs; awake off it after, it is kept there by steering, never put again", "[physics][rest-placer]")
{
    using Step = RestPlacer::Step;
    RestPlacer placer(0, false);
    REQUIRE(placer.Next(true, false, true, false, false, 0) == Step::kSleep);
    placer.Slept(false, 0);
    CHECK_FALSE(placer.Done);
    placer.Slept(false, RestPlacer::kSleepMs);
    CHECK(placer.Done);
    // Nothing holds it up here: steered back for as long as it is awake off its rest, however often.
    for (uint64_t tick = 3000; tick < 60000; tick += 1000)
        CHECK(placer.Next(false, false, false, false, true, tick) == Step::kKeep);
    THEN("back at it but not still yet, it is steered on; still, put to sleep again; asleep, left")
    {
        CHECK(placer.Next(true, false, false, false, false, 61000) == Step::kKeep);
        CHECK(placer.Next(true, false, true, false, false, 61016) == Step::kSleep);
        CHECK(placer.Next(true, false, true, true, false, 61032) == Step::kLeave);
    }
    THEN("one that was never off its rest is not steered for being awake (the bar's bottles)")
    {
        RestPlacer still(0, false);
        (void)still.Next(true, false, true, false, false, 0);
        still.Slept(true, 0);
        CHECK(still.Next(true, false, false, false, false, 100) == Step::kLeave);
    }
}

TEST_CASE("A body Havok pairs with one of this game's is knocked only once it leaves where it lay", "[physics][authority]")
{
    PairedPoses paired;
    const BodyPose cLay{.Key = kPlate, .Position = {10.f, 0.f, 0.f}};
    paired.Update({{kPlate, cLay}});
    CHECK_FALSE(paired.Moved(kPlate));
    BodyPose now = cLay;
    now.Position.x += 0.3f; // a settle in place
    paired.Update({{kPlate, now}});
    CHECK_FALSE(paired.Moved(kPlate));
    now.Position.x += 0.4f;
    paired.Update({{kPlate, now}});
    CHECK(paired.Moved(kPlate));
    THEN("no longer paired, it is forgotten")
    {
        paired.Update({});
        CHECK_FALSE(paired.Moved(kPlate));
    }
    THEN("a shift before the first read counts from its last rest, when near")
    {
        PairedPoses fresh;
        fresh.Update({{kPlate, BodyPose{.Key = kPlate, .Position = {11.5f, 0.f, 0.f}}}}, [&](const BodyKey&) { return std::optional(cLay); });
        CHECK(fresh.Moved(kPlate));
    }
    THEN("the bodies it does not move are not taken")
    {
        BodyAuthority authority;
        authority.Touch(kBucket, kGuest, 0);
        CHECK(authority.Spread(kGuest, {{kBucket, kCup}}, 0, [](const BodyKey&) { return false; }).empty());
        CHECK(authority.Spread(kGuest, {{kBucket, kCup}}, 0, [](const BodyKey&) { return true; }).size() == 1);
    }
}

TEST_CASE("The host taking a body back with the number of a rest it never got is no confirmation", "[physics][authority]")
{
    BodyAuthority host, guest;
    guest.Touch(kBucket, kGuest, 0);
    REQUIRE(host.Accept(kGuest, StateOf(guest, kBucket, kGuest)));
    guest.Rest(kGuest, {kBucket}, {}, 1000); // lost on the way
    host.Unclaim(kBucket);                   // the host thought it silent
    REQUIRE(host.Of(kBucket).AuthSeq == guest.Of(kBucket).AuthSeq);
    // Moving in the host's game: shown here, nobody's.
    CHECK(guest.Hear(StateOf(host, kBucket, kGuest), kGuest) == BodyAuthority::Heard::kApply);
    CHECK(guest.Is(kBucket, BodyAuthority::kNobody));
}

#include "../FakeFlight.h"
#include <Physics/BodyMotions.h>
#include <Physics/BodyPlayback.h>
#include <Physics/BodyPrediction.h>

namespace
{
// What a watcher shows of another game's slide: the owner's body through the stream, the playback 100 ms behind
// and the steer, a frame at a time.
struct Watched
{
    float Past{};     // furthest past where the owner's stopped
    float Back{};     // units it moved backwards in all
    uint64_t Late{};  // ms after the owner's stop until it lay within a settle of it for good
    size_t Sent{};
};

Watched Watch(float aSpeed)
{
    FakeFlight flight;
    flight.Body.Key = {.Object = GameId(0, 1)};
    flight.Body.LinearVelocity = {aSpeed, 0.f, 0.f};
    BodyStream stream;
    BodyMotions motions;
    BodyPlayback playback;
    BodyPrediction::Still still;
    glm::vec3 shown{0.f};
    const glm::quat cTurn{1.f, 0.f, 0.f, 0.f};
    bool rested = false;
    std::optional<glm::vec3> rest;
    float lastX = 0.f;
    uint64_t stopped = 0;
    Watched watched;
    for (uint64_t tick = 1000; tick < 6000; tick += 16)
    {
        flight.Step(0.016f);
        if (flight.Resting && !stopped)
            stopped = tick;
        std::vector<BodyFrame> awake{flight.Body};
        motions.Mark(awake, tick);
        BodyState state{.Frame = awake[0], .AuthSeq = 1};
        rested = rested || (still.Read(flight.Body, tick) && tick - 1000 > BodyTolerances::kLeanMs);
        if (rested)
        {
            state.AtRest = true;
            state.AuthSeq = 2;
        }
        for (const BodyState& cDue : stream.Due({state}, tick))
        {
            ++watched.Sent;
            if (cDue.AtRest)
                playback.Rest(tick, {{.Key = cDue.Frame.Key, .Position = cDue.Frame.Position, .Rotation = cDue.Frame.Rotation}});
            else
                playback.Add(tick, {cDue.Frame});
        }
        for (const auto& cPose : playback.At(tick - 100))
        {
            if (cPose.Rest)
                rest = cPose.Body.Position;
            else
                shown += BodyPrediction::Follow(shown, cTurn, cPose.Body, 0.016f).Linear * 0.016f;
        }
        // RestPlacer: near, put there; off, glided.
        if (rest && glm::distance(shown, *rest) >= BodyTolerances::kSameDistance)
            shown += (*rest - shown) * (0.016f / BodyPrediction::kRestGlideStep);
        if (stopped)
        {
            watched.Past = std::max(watched.Past, shown.x - flight.Body.Position.x);
            if (glm::distance(shown, flight.Body.Position) >= BodyTolerances::kSameDistance)
                watched.Late = tick + 16 - stopped;
        }
        watched.Back += std::max(0.f, lastX - shown.x);
        lastX = shown.x;
    }
    return watched;
}
} // namespace

TEST_CASE("A watcher's object stops where its owner's does: never past it, never back, soon after", "[physics][stream][watcher]")
{
    // The user's playtest, 2026-11-23: another player's push slid on past its stop and was smoothed back, every time.
    // Measured before: 1.5 to 2.6 units past, 1.1 to 2.2 back, settled 0.5 s late (a guess with no friction, sent on a
    // 2 unit error).
    for (const float cSpeed : {120.f, 300.f})
    {
        const Watched cWatched = Watch(cSpeed);
        INFO("pushed at " << cSpeed << ": " << cWatched.Past << " past, " << cWatched.Back << " back, " << cWatched.Late << " ms late, " << cWatched.Sent
                          << " states");
        CHECK(cWatched.Past < 0.25f);
        CHECK(cWatched.Back < 0.25f);
        CHECK(cWatched.Late <= 250);
    }
}

TEST_CASE("A held body played on another game never turns back between its states, whatever speeds its hand gives", "[physics][watcher]")
{
    // The duo, 2026-11-23 (carry_into_pile): the host's copy of a carried object turned back 97 times (vanilla up to
    // 19): a curve through the hand's states with their jumping speeds looped past the next.
    BodyPlayback playback;
    const BodyKey cKey{.Object = GameId(0, 1)};
    const float cSpeeds[] = {400.f, -150.f, 600.f, 20.f, -300.f, 500.f};
    for (int i = 0; i < 6; ++i)
        playback.Add(1000 + 45 * static_cast<uint64_t>(i), {{.Key = cKey, .Position = {3.f * static_cast<float>(i), 0.f, 0.f}, .LinearVelocity = {cSpeeds[i], 0.f, 0.f}}});
    float last = -1.f;
    int turns = 0;
    for (uint64_t tick = 1000; tick <= 1225; tick += 5)
        for (const auto& cPose : playback.At(tick))
        {
            turns += cPose.Body.Position.x < last - 1e-4f;
            last = cPose.Body.Position.x;
        }
    CHECK(turns == 0);
}

TEST_CASE("A knock goes on the stream at once, whenever the last state went", "[physics][stream]")
{
    BodyStream stream;
    BodyState state{.Frame = {.Key = {.Object = GameId(0, 1)}, .LinearVelocity = {10.f, 0.f, 0.f}}, .AuthSeq = 1};
    for (uint64_t tick = 1000; tick < 1000 + 16 * BodyTolerances::kChangedRepeats; tick += 16)
        REQUIRE(stream.Due({state}, tick).size() == 1);
    const uint64_t cNext = 1000 + 16 * BodyTolerances::kChangedRepeats;
    state.Frame.Position.x = 0.3f;
    CHECK(stream.Due({state}, cNext).empty()); // moving on: within kMovingMs of the last
    state.Frame.LinearVelocity = {350.f, 0.f, 0.f};
    CHECK(stream.Due({state}, cNext + 1).size() == 1);
}
