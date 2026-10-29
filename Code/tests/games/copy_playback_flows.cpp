// A copy's playback time (CopyClock, CopyPlayback; docs/design/copy-clock.md): as far behind its owner as the line
// needs, moving on smoothly when snapshots come late.
#include "Party.h"

#include <Components.h>
#include <Systems/InterpolationSystem.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E; // the cell everyone stands in
constexpr uint32_t kGuard = 0x0001A67C; // an NPC's base

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

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
    Party party(1, Plugins);
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

SCENARIO("Over a slow, jittery line a walking copy never steps back", "[copies][copy-clock]")
{
    Party party(1, Plugins, true, NetConditions{.DelayMs = 80, .JitterMs = 40, .Seed = 3});
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
    Party party(1, Plugins);
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
