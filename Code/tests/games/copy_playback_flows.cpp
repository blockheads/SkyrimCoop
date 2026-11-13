// A copy's playback time (CopyClock, CopyPlayback; docs/design/copy-clock.md): as far behind its owner as the line
// needs, moving on smoothly when snapshots come late.
#include "Party.h"
#include "Fixtures.h"

#include <Components.h>
#include <Systems/InterpolationSystem.h>
#include <Services/CharacterService.h>
#include <Messages/NotifyCharacterSpawn.h>

#include <set>
#include <thread>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E; // the cell everyone stands in
constexpr uint32_t kGuard = 0x0001A67C; // an NPC's base

// The guest's copy of the host's player, 0 before it has one.
uint32_t HostPlayerCopy(FakeEngine& aGuest)
{
    for (const uint32_t cId : aGuest.CopiesOf())
        if (aGuest.Actors.at(cId).Player)
            return cId;
    return 0;
}

// aGame's entity for its copy aCopy, entt::null without one.
entt::entity CopyEntity(World& aGame, uint32_t aCopy)
{
    for (const auto cEntity : aGame.view<InterpolationComponent, FormIdComponent>())
        if (aGame.get<FormIdComponent>(cEntity).Id == aCopy)
            return cEntity;
    return entt::null;
}

// The time aGame shows its copy aCopy at, this frame.
uint64_t CopyTick(World& aGame, uint32_t aCopy)
{
    const auto cEntity = CopyEntity(aGame, aCopy);
    return cEntity != entt::null ? InterpolationSystem::PlaybackTick(aGame, cEntity) : 0;
}

// How often aCopy's owner sends it, as aGame measures (CopyPlayback's target interval), 0 without a copy.
double SendInterval(World& aGame, uint32_t aCopy)
{
    const auto cEntity = CopyEntity(aGame, aCopy);
    return cEntity != entt::null ? aGame.get<InterpolationComponent>(cEntity).Playback.TargetInterval() : 0.0;
}

// Frames for aMs of wall time (the session's clock), the host's player walking along x at aSpeed a second.
void Walk(Party& aParty, std::chrono::milliseconds aMs, float aSpeed, const std::function<void()>& acEachFrame = {})
{
    auto& host = Party::Engine(aParty.Host());
    const auto cStart = std::chrono::steady_clock::now();
    const float cFrom = host.Actors.at(FakeEngine::kPlayer).Position[0];
    while (std::chrono::steady_clock::now() - cStart < aMs)
    {
        const float cSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - cStart).count();
        host.Actors.at(FakeEngine::kPlayer).Position[0] = cFrom + aSpeed * cSeconds;
        aParty.Frame();
        if (acEachFrame)
            acEachFrame();
    }
}
} // namespace

TEST_CASE("A copy whose next snapshot is late carries on its way a little, then holds", "[copy-clock]")
{
    InterpolationComponent::TimePoint first, second;
    first.Tick = 1000;
    second.Tick = 1100;
    first.OnGround = second.OnGround = true;
    second.Position = {10.f, 0.f, 0.f};
    glm::vec3 position{}, rotation{};

    REQUIRE(InterpolationSystem::Interpolate(first, second, 1150, position, rotation, 100));
    CHECK(position.x == Approx(15.f));
    REQUIRE(InterpolationSystem::Interpolate(first, second, 1400, position, rotation, 100));
    CHECK(position.x == Approx(20.f));
    SECTION("not without a limit")
    {
        REQUIRE(InterpolationSystem::Interpolate(first, second, 1150, position, rotation));
        CHECK(position.x == Approx(10.f));
    }
    SECTION("off the ground only across: placed, it would sink through the floor at a landing")
    {
        second.Position.z = -30.f;
        second.OnGround = false;
        REQUIRE(InterpolationSystem::Interpolate(first, second, 1150, position, rotation, 100));
        CHECK(position.x == Approx(15.f));
        CHECK(position.z == Approx(-30.f));
    }
    SECTION("not while its owner lies in its ragdoll: its physics places it")
    {
        second.Lying = true;
        first.Lying = true;
        REQUIRE(InterpolationSystem::Interpolate(first, second, 1150, position, rotation, 100));
        CHECK(position.x == Approx(10.f));
    }
}

SCENARIO("A copy plays its owner's time as far behind as the line needs, not a fixed 300 ms", "[copies][copy-clock]")
{
    Party party(1, SmokeWorld);
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return HostPlayerCopy(guest) != 0; }, "the guest's game makes a copy of the host's player");
    Walk(party, std::chrono::milliseconds(1500), 100.f);

    const uint32_t cCopy = HostPlayerCopy(guest);
    const uint64_t cNow = party.Guest().GetTransport().GetTick();
    const uint64_t cShown = CopyTick(party.Guest(), cCopy);
    INFO("network delay " << party.Guest().GetCopyClock().Delay() << " ms, copy " << cNow - cShown << " ms behind");
    CHECK(party.Guest().GetCopyClock().Delay() < 60); // in this process: a frame and the margin
    CHECK(cNow - cShown < 200);                       // and its owner's send interval
}

SCENARIO("A copy's capsule collides only when its owner's does: in furniture it is not pushed off", "[copies][copy-step]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return HostPlayerCopy(guest) != 0; }, "the guest's game makes a copy of the host's player");
    Walk(party, std::chrono::milliseconds(300), 100.f);
    const uint32_t cCopy = HostPlayerCopy(guest);
    auto& owner = host.Actors.at(FakeEngine::kPlayer);
    auto& copy = guest.Actors.at(cCopy);
    // Moved along x: y shows a push from its furniture.
    const auto cFurthestOff = [&](std::chrono::milliseconds aMs) {
        const float cPathY = owner.Position[1];
        float furthest = 0.f;
        Walk(party, aMs, 100.f, [&] { furthest = std::max(furthest, std::abs(copy.Position[1] - cPathY)); });
        return furthest;
    };

    WHEN("its capsule still collides in its furniture")
    {
        copy.InFurniture = true;
        THEN("its furniture pushes it off its owner's path each step: the shake") { CHECK(cFurthestOff(std::chrono::milliseconds(300)) >= FakeEngine::kFurnitureOffset); }
    }
    WHEN("its owner sits, its capsule collision off (40063)")
    {
        owner.Collides = false;
        party.Until([&] { return !copy.Collides; }, "the copy's capsule follows its owner's");
        copy.InFurniture = true;
        THEN("the copy is placed on its owner's path, its collision kept off")
        {
            CHECK(cFurthestOff(std::chrono::milliseconds(600)) < 0.1f);
            CHECK_FALSE(copy.Stepped);
            CHECK_FALSE(copy.Collides);
        }
        AND_WHEN("its owner gets up, its collision on again")
        {
            owner.Collides = true;
            copy.InFurniture = false;
            party.Until([&] { return copy.Collides && copy.Stepped; }, "the copy collides and is stepped again");
        }
    }
    WHEN("its owner stands with its collision off (far off the host's player: MoveOnNavmeshUnseen)")
    {
        owner.Collides = false;
        party.Until([&] { return !copy.Collides; }, "the copy's capsule follows its owner's");
        cFurthestOff(std::chrono::milliseconds(300));
        THEN("a step would turn it back on (Actor::Move): the copy is placed and stays off")
        {
            CHECK_FALSE(copy.Stepped);
            CHECK_FALSE(copy.Collides);
        }
    }
}

SCENARIO("Over a slow, jittery line a walking copy never steps back", "[copies][copy-clock]")
{
    Party party(1, SmokeWorld, true, NetConditions{.DelayMs = 80, .JitterMs = 40, .Seed = 3});
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return HostPlayerCopy(guest) != 0; }, "the guest's game makes a copy of the host's player");
    const uint32_t cCopy = HostPlayerCopy(guest);
    Walk(party, std::chrono::milliseconds(1500), 200.f); // the clock learns the line

    float previous = guest.Actors.at(cCopy).Position[0];
    float worst = 0.f;
    Walk(party, std::chrono::milliseconds(3000), 200.f, [&] {
        const float cNow = guest.Actors.at(cCopy).Position[0];
        worst = std::max(worst, previous - cNow);
        previous = cNow;
    });
    INFO("network delay " << party.Guest().GetCopyClock().Delay() << " ms");
    CHECK(worst < 0.5f);
    CHECK(party.Guest().GetCopyClock().Network() >= 80);
    CHECK(party.Guest().GetCopyClock().Network() <= 80 + 40 + 50);
}

SCENARIO("Players and fights are sent 30 times a second, other actors 10", "[copies][copy-clock]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t idle = 0, fighting = 0;
    party.As(party.Host(), [&] {
        idle = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f});
        fighting = host.LoadNpc(kGuard, kSmoke, {-30.f, 40.f, 0.f});
    });
    host.Actors.at(fighting).Ai.CombatState = 1;
    party.Until([&] { return HostPlayerCopy(guest) && guest.CopiesOf(kGuard).size() == 2; }, "the guest's game copies the player and both guards");
    Walk(party, std::chrono::milliseconds(1500), 100.f);

    // The guards' copies by where they stand: the fighter on the left.
    uint32_t idleCopy = 0, fightingCopy = 0;
    for (const uint32_t cId : guest.CopiesOf(kGuard))
        (guest.Actors.at(cId).Position[0] < 0.f ? fightingCopy : idleCopy) = cId;
    CHECK(SendInterval(party.Guest(), HostPlayerCopy(guest)) == Approx(33).margin(8));
    CHECK(SendInterval(party.Guest(), fightingCopy) == Approx(33).margin(8));
    CHECK(SendInterval(party.Guest(), idleCopy) == Approx(100).margin(10));
}

SCENARIO("A strafing player's copy turns its animation with its movement", "[copies][copy-clock]")
{
    // S10: the owner's strafe direction flips the frame it turns; its copy's must flip as its movement turns, within
    // half a round, not a whole snapshot later (sliding the new way in the old strafe). Its graph's variables come in
    // its owner's stream, on the playback clock its movement is shown on.
    Party party(1, SmokeWorld, true);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    for (auto* engine : {&host, &guest})
    {
        engine->Graph = FakeGraphRules{};
        engine->Actors.at(FakeEngine::kPlayer).GraphState = "Standing";
    }
    party.Until([&] { return HostPlayerCopy(guest) != 0; }, "the guest's game makes a copy of the host's player");
    const uint32_t cCopy = HostPlayerCopy(guest);
    party.Until([&] { return party.Guest().GetGraphStreams().OnStream(cCopy); }, "the copy is on its owner's stream");
    auto& graph = host.Actors.at(FakeEngine::kPlayer).GraphVariables;
    graph.Floats = {0.25f, 340.f}; // Direction (right), Speed
    Walk(party, std::chrono::milliseconds(800), 200.f);

    using Clock = std::chrono::steady_clock;
    struct Seen { Clock::time_point Time; float X; float Direction; };
    for (int turn = 0; turn < 8; ++turn)
    {
        // Turns at times unrelated to the rounds: 0-30 ms after one.
        const float cSpeed = turn % 2 ? 200.f : -200.f;
        graph.Floats[0] = turn % 2 ? 0.25f : 0.75f;
        std::vector<Seen> seen;
        Walk(party, std::chrono::milliseconds(400 + 7 * turn), cSpeed, [&] {
            const auto& cActor = guest.Actors.at(cCopy);
            seen.push_back({Clock::now(), cActor.Position[0], cActor.GraphVariables.Floats.empty() ? 0.f : cActor.GraphVariables.Floats[0]});
        });
        // When the copy started moving the new way, and when its direction flipped.
        const auto cTurned = std::ranges::find_if(seen.begin() + 1, seen.end(), [&](const Seen& acSeen) {
            const auto& cBefore = *(&acSeen - 1);
            return (acSeen.X - cBefore.X) * cSpeed > 0.f;
        });
        const auto cFlipped = std::ranges::find_if(seen, [&](const Seen& acSeen) { return acSeen.Direction == graph.Floats[0]; });
        REQUIRE(cTurned != seen.end());
        REQUIRE(cFlipped != seen.end());
        const auto cApartMs = std::chrono::duration_cast<std::chrono::milliseconds>(cFlipped->Time - cTurned->Time).count();
        INFO("turn " << turn << ": the direction flipped " << cApartMs << " ms after the movement turned");
        CHECK(std::abs(cApartMs) <= 17);
    }
}

SCENARIO("A player's actions play on its copy where its movement was when it acted", "[copies][copy-clock][animation]")
{
    // S10: the engine's frames are steady, our update lands 0-11 ms into each, where the session's time is read.
    // Owners stamp movement with the frame clock; actions stamped with the session's time as read land off their
    // movement on the copy's one timeline, unevenly (red-flags.md, 1).
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return HostPlayerCopy(guest) != 0; }, "the guest's game makes a copy of the host's player");
    const uint32_t cCopy = HostPlayerCopy(guest);
    constexpr float cSpeed = 600.f;
    constexpr double cFrameMs = 1000.0 / 60.0;
    host.FrameMs = guest.FrameMs = cFrameMs;

    struct Seen { uint64_t Tick; float X; };
    std::vector<Seen> seen; // the copy's playback time and place, each frame
    std::vector<std::pair<uint64_t, float>> acted; // each action's stamp, and where its owner stood
    const auto cStart = std::chrono::steady_clock::now();
    const float cFrom = host.Actors.at(FakeEngine::kPlayer).Position[0];
    for (int frame = 0; frame < 120; ++frame)
    {
        // The engine's frame: it moves the player and performs its actions, then our update runs, late by turns.
        std::this_thread::sleep_until(cStart + std::chrono::duration<double, std::milli>(frame * cFrameMs));
        host.Actors.at(FakeEngine::kPlayer).Position[0] = cFrom + cSpeed * static_cast<float>(frame * cFrameMs / 1000.0);
        if (frame >= 40 && frame % 7 == 0)
        {
            ActionEvent gesture;
            gesture.ActionId = 0x489F1;
            gesture.IdleId = frame % 2 ? 0xF779D : 0x10DEB8;
            gesture.EventName = "MotionDrivenIdle";
            party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(FakeEngine::kPlayer, gesture)); });
            acted.emplace_back(host.Actors.at(FakeEngine::kPlayer).LatestAnimation.Tick, host.Actors.at(FakeEngine::kPlayer).Position[0]);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(frame % 2 ? 11 : 0));
        party.Frame();
        seen.push_back({CopyTick(party.Guest(), cCopy), guest.Actors.at(cCopy).Position[0]});
    }
    party.Until([&] {
        seen.push_back({CopyTick(party.Guest(), cCopy), guest.Actors.at(cCopy).Position[0]});
        return seen.back().Tick > acted.back().first;
    }, "the copy's playback passes the last action");

    // Where the copy's movement is at each action's stamp, against where its owner acted.
    float lowest = 1e9f, highest = -1e9f;
    for (const auto& [cStamp, cX] : acted)
    {
        const auto cAfter = std::ranges::find_if(seen, [&](const Seen& acSeen) { return acSeen.Tick >= cStamp; });
        REQUIRE(cAfter != seen.end());
        REQUIRE(cAfter != seen.begin());
        const auto& cBefore = *std::prev(cAfter);
        const double cPart = cAfter->Tick > cBefore.Tick ? double(cStamp - cBefore.Tick) / double(cAfter->Tick - cBefore.Tick) : 1.0;
        const float cShown = cBefore.X + static_cast<float>(cPart) * (cAfter->X - cBefore.X);
        lowest = std::min(lowest, cShown - cX);
        highest = std::max(highest, cShown - cX);
    }
    INFO(acted.size() << " actions: the copy's movement stood " << lowest << " to " << highest << " units from where its owner acted ("
                      << cSpeed * cFrameMs / 1000.0 << " a frame)");
    CHECK(highest - lowest <= 2.f); // the same each time: within 3 ms
    CHECK(std::abs(lowest) <= cSpeed * cFrameMs / 1000.0 + 2.f); // at most a frame off
}

SCENARIO("A walking player's copy steps as evenly as its owner", "[copies][copy-clock][wire]")
{
    // Skyrim: a walking copy stepped 0.9 to 1.55 units a frame against its owner's 1.4; its place went in whole units
    // (copy-clock.md, "Measuring smoothness").
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return HostPlayerCopy(guest) != 0; }, "the guest's game makes a copy of the host's player");
    const uint32_t cCopy = HostPlayerCopy(guest);
    constexpr double cFrameMs = 1000.0 / 60.0;
    const glm::vec2 cVelocity{84.f, -3.f};
    host.FrameMs = guest.FrameMs = cFrameMs;

    const auto cStart = std::chrono::steady_clock::now();
    const glm::vec2 cFrom{host.Actors.at(FakeEngine::kPlayer).Position[0] + 0.3f, host.Actors.at(FakeEngine::kPlayer).Position[1] + 0.6f};
    std::vector<glm::vec2> seen;
    for (int frame = 0; frame < 300; ++frame)
    {
        std::this_thread::sleep_until(cStart + std::chrono::duration<double, std::milli>(frame * cFrameMs));
        const glm::vec2 cAt = cFrom + cVelocity * static_cast<float>(frame * cFrameMs / 1000.0);
        host.Actors.at(FakeEngine::kPlayer).Position[0] = cAt.x;
        host.Actors.at(FakeEngine::kPlayer).Position[1] = cAt.y;
        std::this_thread::sleep_for(std::chrono::milliseconds(frame % 2 ? 11 : 0));
        party.Frame();
        if (frame >= 120)
            seen.emplace_back(guest.Actors.at(cCopy).Position[0], guest.Actors.at(cCopy).Position[1]);
    }

    const float cOwnerStep = glm::length(cVelocity) * static_cast<float>(cFrameMs / 1000.0);
    size_t uneven = 0;
    float lowest = 1e9f, highest = 0.f;
    for (size_t i = 1; i < seen.size(); ++i)
    {
        const float cStep = glm::length(seen[i] - seen[i - 1]);
        lowest = std::min(lowest, cStep);
        highest = std::max(highest, cStep);
        uneven += std::abs(cStep - cOwnerStep) > 0.2f * cOwnerStep;
    }
    INFO(uneven << " of " << seen.size() - 1 << " frames over a fifth off its owner's steady " << cOwnerStep << " units (steps "
                << lowest << " to " << highest << ")");
    CHECK(uneven <= 2);
}

SCENARIO("A guest's player moves evenly on the host's game though its clock runs ahead", "[copies][copy-clock]")
{
    // Skyrim showed a guest's stamps arriving ahead of the host's time as read: 24% of them, by 9 ms on average
    // (red-flags.md, 2). Its clock runs ahead here, past the in-process delivery; its stamps go to copies as they are.
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return HostPlayerCopy(host) != 0; }, "the host's game makes a copy of the guest's player");
    const uint32_t cCopy = HostPlayerCopy(host);
    constexpr float cSpeed = 300.f;
    constexpr double cFrameMs = 1000.0 / 60.0;
    constexpr uint64_t cAheadMs = 60;
    host.FrameMs = guest.FrameMs = cFrameMs;
    auto& clock = const_cast<Net::SynchronizedClock&>(party.Guest().GetTransport().GetClock());

    const auto cStart = std::chrono::steady_clock::now();
    const float cFrom = guest.Actors.at(FakeEngine::kPlayer).Position[0];
    std::vector<float> seen;
    std::set<uint64_t> stamped;  // the guest's frame times, which its snapshots carry
    std::set<uint64_t> received; // the stamps of the host copy's snapshots
    for (int frame = 0; frame < 360; ++frame)
    {
        // Steady engine frames (S10).
        std::this_thread::sleep_until(cStart + std::chrono::duration<double, std::milli>(frame * cFrameMs));
        guest.Actors.at(FakeEngine::kPlayer).Position[0] = cFrom + cSpeed * static_cast<float>(frame * cFrameMs / 1000.0);
        if (frame % 30 == 0) // as a sync would, eased in over a second (SynchronizedClock)
        {
            clock.Update();
            clock.Synchronize(party.Host().GetTick() + cAheadMs, 0);
        }
        // Two processes: the guest's update lands 0-11 ms into its frame, the host's 6, so the host takes the
        // guest's snapshot in the same frame or the next.
        const auto cFrameStart = cStart + std::chrono::duration<double, std::milli>(frame * cFrameMs);
        constexpr int cGuestLate[] = {0, 11, 3};
        const auto cGuestAt = cFrameStart + std::chrono::milliseconds(cGuestLate[frame % 3]);
        const auto cHostAt = cFrameStart + std::chrono::milliseconds(6);
        const bool cGuestFirst = cGuestAt < cHostAt;
        std::this_thread::sleep_until(cGuestFirst ? cGuestAt : cHostAt);
        const auto cStep = [&](World& aGame) {
            party.Step(aGame);
            if (&aGame == &party.Guest())
                stamped.insert(party.Guest().GetFrameTick());
            else if (const auto cEntity = CopyEntity(party.Host(), cCopy); frame >= 180 && cEntity != entt::null)
                for (const auto& cPoint : party.Host().get<InterpolationComponent>(cEntity).TimePoints)
                    received.insert(cPoint.Tick);
        };
        cStep(cGuestFirst ? party.Guest() : party.Host());
        std::this_thread::sleep_until(cGuestFirst ? cHostAt : cGuestAt);
        cStep(cGuestFirst ? party.Host() : party.Guest());
        if (frame >= 180)
            seen.push_back(host.Actors.at(cCopy).Position[0]);
    }
    size_t changed = 0;
    for (const uint64_t cTick : received)
        changed += !stamped.contains(cTick);
    REQUIRE(received.size() > 50);

    // Frames where the copy's step is off its owner's steady one by over a third.
    const float cOwnerStep = cSpeed * static_cast<float>(cFrameMs / 1000.0);
    size_t uneven = 0;
    for (size_t i = 1; i < seen.size(); ++i)
        if (std::abs((seen[i] - seen[i - 1]) - cOwnerStep) > cOwnerStep / 3.f)
            ++uneven;
    INFO(changed << " of " << received.size() << " snapshots reached the copy with a stamp the guest never sent");
    CHECK(changed == 0);
    INFO(uneven << " of " << seen.size() - 1 << " frames the copy stepped over a third off its owner's steady step");
    CHECK(uneven <= 2);
}

SCENARIO("Over a lossy line a guest's player reaches the host without stalls", "[copies][copy-clock][net]")
{
    // A lost reliable snapshot holds back every later one until its resend; unreliable, only itself (red-flags.md, 5).
    Party party(1, SmokeWorld, true, NetConditions{.DelayMs = 30, .JitterMs = 10, .LossPercent = 5, .Seed = 7});
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return HostPlayerCopy(host) != 0; }, "the host's game makes a copy of the guest's player");
    const uint32_t cCopy = HostPlayerCopy(host);

    using Clock = std::chrono::steady_clock;
    const auto cStart = Clock::now();
    const float cFrom = guest.Actors.at(FakeEngine::kPlayer).Position[0];
    uint64_t newest = 0;
    auto lastArrival = cStart;
    double longestGapMs = 0.0;
    size_t arrivals = 0;
    while (Clock::now() - cStart < std::chrono::seconds(4))
    {
        const float cSeconds = std::chrono::duration<float>(Clock::now() - cStart).count();
        guest.Actors.at(FakeEngine::kPlayer).Position[0] = cFrom + 200.f * cSeconds;
        party.Frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto cEntity = CopyEntity(party.Host(), cCopy);
        if (cEntity == entt::null)
            continue;
        const auto& cPoints = party.Host().get<InterpolationComponent>(cEntity).TimePoints;
        const uint64_t cNewest = cPoints.empty() ? 0 : cPoints.back().Tick;
        if (cNewest > newest)
        {
            if (newest && cSeconds > 0.5f)
            {
                longestGapMs = std::max(longestGapMs, std::chrono::duration<double, std::milli>(Clock::now() - lastArrival).count());
                ++arrivals;
            }
            newest = cNewest;
            lastArrival = Clock::now();
        }
    }
    REQUIRE(arrivals > 50);
    INFO("the longest wait for the guest's next snapshot: " << longestGapMs << " ms, over " << arrivals << " arrivals");
    CHECK(longestGapMs < 120.0); // a lost one or two: a few send intervals
}

SCENARIO("Over a lossy line a strafing player's copy never jumps back where its guess overshot", "[copies][copy-clock][net]")
{
    // A lost snapshot at a turn made the copy's short guess overshoot, and the late one snapped it back
    // (copy-clock.md, "Measuring smoothness").
    Party party(1, SmokeWorld, true, NetConditions{.DelayMs = 30, .JitterMs = 10, .LossPercent = 15, .Seed = 11});
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return HostPlayerCopy(host) != 0; }, "the host's game makes a copy of the guest's player");
    const uint32_t cCopy = HostPlayerCopy(host);
    constexpr double cFrameMs = 1000.0 / 60.0;
    constexpr float cSpeed = 300.f;
    host.FrameMs = guest.FrameMs = cFrameMs;

    const auto cStart = std::chrono::steady_clock::now();
    float x = guest.Actors.at(FakeEngine::kPlayer).Position[0];
    std::vector<float> seen;
    for (int frame = 0; frame < 420; ++frame)
    {
        std::this_thread::sleep_until(cStart + std::chrono::duration<double, std::milli>(frame * cFrameMs));
        x += ((frame / 20) % 2 ? -cSpeed : cSpeed) * static_cast<float>(cFrameMs / 1000.0); // turns every third of a second
        guest.Actors.at(FakeEngine::kPlayer).Position[0] = x;
        party.Frame();
        if (frame >= 120)
            seen.push_back(host.Actors.at(cCopy).Position[0]);
    }
    const float cOwnerStep = cSpeed * static_cast<float>(cFrameMs / 1000.0);
    float biggest = 0.f;
    size_t jumps = 0;
    for (size_t i = 1; i < seen.size(); ++i)
    {
        const float cStep = std::abs(seen[i] - seen[i - 1]);
        biggest = std::max(biggest, cStep);
        jumps += cStep > 2.f * cOwnerStep;
    }
    INFO(jumps << " frames the copy moved over twice its owner's " << cOwnerStep << " units; the biggest " << biggest);
    CHECK(jumps == 0);
}

SCENARIO("A refreshed spawn drops a copy's guess and correction with its points", "[copies][copy-clock][net]")
{
    // A refreshed spawn (an arrival) drops a copy's points; a correction against its old guess would pull it back
    // (copy-clock.md, "Measuring smoothness").
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t wolf = 0;
    party.As(party.Host(), [&] { wolf = host.LoadNpc(0x23ABE, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(0x23ABE).size() == 1; }, "the wolf's copy appears");
    const uint32_t cCopy = guest.CopiesOf(0x23ABE).front();
    const auto cEntity = CopyEntity(party.Guest(), cCopy);
    REQUIRE((cEntity != entt::null));
    auto& interpolation = party.Guest().get<InterpolationComponent>(cEntity);
    InterpolationComponent::TimePoint cOld;
    cOld.Tick = 1;
    cOld.Position = {-500.f, 0.f, 0.f};
    interpolation.Guess.emplace(cOld, cOld);
    interpolation.Correction.Begin({30.f, 0.f, 0.f}, party.Guest().GetFrameTick());

    std::vector<NotifyCharacterSpawn> spawns;
    party.As(party.Host(), [&] { spawns = party.Host().GetCharacterService().BuildSpawnRequests(0); });
    party.As(party.Guest(), [&] {
        for (const auto& cSpawn : spawns)
            if (cSpawn.SessionId == party.Guest().get<RemoteComponent>(cEntity).Id)
                party.Guest().GetCharacterService().OnCharacterSpawn(cSpawn);
    });
    CHECK_FALSE(interpolation.Guess.has_value());
    CHECK(interpolation.Correction.At(party.Guest().GetFrameTick()) == glm::vec3{});
}

SCENARIO("A jumping player's copy rises and falls with it, never stuck on the ground", "[copies][copy-clock][copy-step]")
{
    // copy-jumps-smoothly-*: a copy stepped by its engine stayed on the ground while its owner rose, was placed past
    // CopyStep::kSnap and fell by its own gravity, twice a jump (actor-movement.md, "A copy in the air").
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return HostPlayerCopy(guest) && HostPlayerCopy(host); }, "each game makes a copy of the other's player");
    const bool cGuestJumps = GENERATE(true, false);
    auto& owner = cGuestJumps ? guest : host;
    auto& watcher = cGuestJumps ? host : guest;
    World& watching = cGuestJumps ? party.Host() : party.Guest();
    const uint32_t cCopy = HostPlayerCopy(watcher);
    const auto cEntity = CopyEntity(watching, cCopy);
    REQUIRE((cEntity != entt::null));
    for (int frame = 0; frame < 30; ++frame)
        party.Frame();

    // Vanilla's standing jump, measured: 74 units up in 0.45 s, down in as long.
    using Clock = std::chrono::steady_clock;
    auto& jumper = owner.Actors.at(FakeEngine::kPlayer);
    const float cGround = jumper.Position[2];
    float worst = 0.f, peak = 0.f;
    for (int jump = 0; jump < 2; ++jump)
    {
        const auto cStart = Clock::now();
        float seconds = 0.f;
        while ((seconds = std::chrono::duration<float>(Clock::now() - cStart).count()) < 1.4f)
        {
            const float cUp = 329.f * seconds - 365.f * seconds * seconds;
            jumper.InAir = cUp > 0.f;
            jumper.Position[2] = cGround + std::max(cUp, 0.f);
            party.Frame();
            const float cShown = watching.get<InterpolationComponent>(cEntity).Position.z;
            const float cHeight = watcher.Actors.at(cCopy).Position[2];
            worst = std::max(worst, std::abs(cHeight - cShown));
            peak = std::max(peak, cHeight - cGround);
        }
    }
    INFO((cGuestJumps ? "the guest" : "the host") << " jumps: its copy peaked " << peak << " up, at worst " << worst
                                                  << " units off its playback's height");
    CHECK(peak > 60.f);
    CHECK(worst < 1.f);
}
