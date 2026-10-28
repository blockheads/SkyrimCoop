// Physics sync across games (docs/design/physics-sync.md): the host's game simulates every body, a guest's shows the
// host's: held keyframed, following the host's flight a little behind, resting exactly where the host's rests.
#include "Party.h"

#include <Services/BodyService.h>
#include <Services/Guest/GuestHands.h>
#include <Messages/Relayed.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
const BodyKey kBucket{.Object = GameId(0, 0x0001A0F3)};
const BodyKey kPlate{.Object = GameId(0, 0x0001A0F4)};
constexpr float kTable = 80.f; // the height they stand at

// Each game's plugins: a bucket and a plate on a table in kSmoke, at rest.
void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
    FakeEngine::Body bucket;
    bucket.Flight.Body.Position = {100.f, 200.f, kTable};
    aEngine.Bodies[kBucket] = bucket;
    FakeEngine::Body plate;
    plate.Flight.Body.Position = {140.f, 200.f, kTable};
    aEngine.Bodies[kPlate] = plate;
}

FakeEngine::Body& BodyOf(Party& aParty, World& aGame, const BodyKey& acKey)
{
    return Party::Engine(aGame).Bodies.at(acKey);
}
} // namespace


SCENARIO("The host's player knocks a bucket off a table: the guest's flies with it and lies where the host's does", "[physics]")
{
    Party party(1, Plugins);
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Knock(kBucket, {120.f, 0.f, 150.f}, {0.f, 6.f, 0.f}); });

    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    party.Until([&] { return guestBucket.Drives > 0; }, "the guest's bucket follows the host's frames");
    THEN("the guest's is held by the host's, not simulated") CHECK(guestBucket.Held);

    const auto cWhere = [&] {
        return std::format("host's at z {:.1f} {}, guest's at z {:.1f}, {} drives, {} rests", hostBucket.Flight.Body.Position.z,
                           hostBucket.Awake ? "awake" : "asleep", guestBucket.Flight.Body.Position.z, guestBucket.Drives, guestBucket.Rests);
    };
    party.Until([&] { return !hostBucket.Awake && guestBucket.Rests > 0; }, "the bucket rests on both games", cWhere);
    CHECK(glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < 0.01f);
    CHECK(std::abs(glm::dot(guestBucket.Flight.Body.Rotation, hostBucket.Flight.Body.Rotation)) > 0.9999f);
    THEN("the plate nobody touched is left alone")
    {
        CHECK(BodyOf(party, party.Guest(), kPlate).Drives == 0);
        CHECK(!BodyOf(party, party.Guest(), kPlate).Held);
    }
}

SCENARIO("A guest whose game has not loaded the body skips the host's frames", "[physics]")
{
    Party party(1, Plugins);
    party.As(party.Guest(), [&] { Party::Engine(party.Guest()).Bodies.erase(kBucket); });
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Knock(kBucket, {0.f, 0.f, 200.f}); });
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    party.Until([&] { return !hostBucket.Awake; }, "the host's bucket rests");
    // A few frames more for the rest to arrive: nothing on the guest to apply it to.
    for (int i = 0; i < 20; ++i)
        party.Frame();
    CHECK(!Party::Engine(party.Guest()).Bodies.contains(kBucket));
}

SCENARIO("An owner's reliable rest waits while the engine cannot settle its body", "[physics][deferred-rest]")
{
    Party party(1, Plugins);
    auto& body = BodyOf(party, party.Guest(), kBucket);
    body.RestBusy = true;
    NotifyBodyRests rest;
    rest.Tick = party.Host().GetTransport().GetTick();
    rest.Rests = {{.Key = kBucket, .Position = {5.f, 6.f, kTable}}};
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(rest); });
    party.Until([&] { return body.RestAttempts > 0; }, "the engine first tries the rest");
    CHECK(body.Rests == 0);
    SECTION("a late moving frame cannot discard the deferred rest")
    {
        NotifyBodyFrames frames;
        frames.Tick = rest.Tick - GENERATE(0, 1);
        frames.Frames = {{.Key = kBucket, .Position = {1.f, 2.f, kTable}}};
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(frames); });
        body.RestBusy = false;
        party.Until([&] { return body.Rests > 0; }, "the engine settles the retained rest when its locks are available");
        CHECK(body.Flight.Body.Position == rest.Rests[0].Position);
        CHECK_FALSE(body.Held);
    }
    SECTION("a newer moving frame supersedes the deferred rest")
    {
        NotifyBodyFrames frames;
        frames.Tick = rest.Tick + 1;
        frames.Frames = {{.Key = kBucket, .Position = {1.f, 2.f, kTable}}};
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(frames); });
        body.RestBusy = false;
        party.Until([&] { return body.Drives > 0; }, "the newer motion is played");
        CHECK(body.Rests == 0);
    }
}

SCENARIO("A guest's player bumps the bucket: the host's is pushed alike, and both lie where the host's does", "[physics]")
{
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = kTable;
    // The guest's character pushes its bucket along the table in its own game (which simulates that flight itself).
    Party::Engine(party.Guest()).PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
    party.Until([&] { return hostBucket.Awake; }, "the host's bucket is pushed");
    const auto cWhere = [&] {
        return std::format("host's at z {:.1f} {}, guest's {} drives, {} rests", hostBucket.Flight.Body.Position.z, hostBucket.Awake ? "awake" : "asleep",
                           guestBucket.Drives, guestBucket.Rests);
    };
    party.Until([&] { return !hostBucket.Awake && guestBucket.Rests > 0; }, "the bucket rests on both games", cWhere);
    THEN("the guest showed its own push, not the host's frames, and lies at the host's rest, simulated again")
    {
        CHECK(guestBucket.Drives == 0);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < 0.01f);
        CHECK(hostBucket.Flight.Body.Position.x > 100.f);
        CHECK_FALSE(guestBucket.Held);
    }
}

SCENARIO("A guest's own blast throws a bucket the host's does not: it gets the host's pose back", "[physics]")
{
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    const glm::vec3 cHost = hostBucket.Flight.Body.Position;
    // The guest's physics alone moves it (its copy of the host's fireball exploded nearer than the host's): awake
    // where it woke, then far off.
    auto& ownMoves = Party::Engine(party.Guest()).OwnMoves;
    ownMoves.push_back({kBucket, guestBucket.Flight.Body.Position, guestBucket.Flight.Body.Rotation});
    guestBucket.Flight.Body.Position += glm::vec3(300.f, 40.f, -80.f);
    ownMoves.push_back({kBucket, guestBucket.Flight.Body.Position, guestBucket.Flight.Body.Rotation});
    party.Until([&] { return guestBucket.Rests > 0; }, "the guest gets the host's pose of it (GuestHands::kOwnMs)");
    CHECK(glm::distance(guestBucket.Flight.Body.Position, cHost) < 0.01f);
    CHECK_FALSE(hostBucket.Awake);
    THEN("a nudge that settles in place asks nothing")
    {
        const size_t cRests = guestBucket.Rests;
        ownMoves.push_back({kBucket, guestBucket.Flight.Body.Position + glm::vec3(0.5f, 0.f, 0.f), guestBucket.Flight.Body.Rotation});
        const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(GuestHands::kOwnMs + 300);
        while (std::chrono::steady_clock::now() < cUntil)
            party.Frame();
        CHECK(guestBucket.Rests == cRests);
    }
}

SCENARIO("A guest's player carries the plate and lets go: the host's follows its hands, then falls from there", "[physics]")
{
    Party party(1, Plugins);
    auto& hostPlate = BodyOf(party, party.Host(), kPlate);
    auto& guestPlate = BodyOf(party, party.Guest(), kPlate);
    hostPlate.Flight.Floor = kTable;
    const glm::vec3 cStart = guestPlate.Flight.Body.Position;
    const glm::vec3 cCarry{150.f, 0.f, 0.f}; // units/s: along the table
    Party::Engine(party.Guest()).Grabbed = kPlate;
    const auto cBegan = std::chrono::steady_clock::now();
    const auto cCarried = [&] { return std::chrono::duration<float>(std::chrono::steady_clock::now() - cBegan).count(); };
    // The guest's hands move it (its own mouse spring), in wall time: the longest frame (a loaded machine's) is how far
    // behind a frame may show it, besides the playback's delay.
    float lastMove = 0.f, longestFrame = 0.f;
    const auto cMove = [&] {
        const float cNow = cCarried();
        longestFrame = std::max(longestFrame, cNow - std::exchange(lastMove, cNow));
        guestPlate.Flight.Body.Position = cStart + cCarry * cNow;
        guestPlate.Flight.Body.LinearVelocity = cCarry;
    };
    party.Until([&] { cMove(); return hostPlate.Drives > 0 && cCarried() > 0.5f; }, "the host's plate follows the guest's hands");
    THEN("the host's is held, a little behind the guest's hands")
    {
        CHECK(hostPlate.Held);
        CHECK_FALSE(hostPlate.Awake);
        const float cBehind = glm::length(cCarry) * (BodyService::kDelayMs / 1000.f + 2.f * longestFrame) + 5.f;
        INFO("longest frame " << longestFrame << " s");
        INFO("host tick " << party.Host().GetTransport().GetTick() << ", guest tick " << party.Guest().GetTransport().GetTick()
             << ", RTT " << party.Guest().GetTransport().GetRoundTripTime() << ", host drives " << hostPlate.Drives
             << ", host x " << hostPlate.Flight.Body.Position.x << ", guest x " << guestPlate.Flight.Body.Position.x);
        CHECK(glm::distance(hostPlate.Flight.Body.Position, guestPlate.Flight.Body.Position) < cBehind);
    }

    const glm::vec3 cLetGo = guestPlate.Flight.Body.Position;
    Party::Engine(party.Guest()).Grabbed.reset();
    party.Until([&] { return !hostPlate.Held; }, "the host's plate is let go");
    party.Until([&] { return !hostPlate.Awake && guestPlate.Rests > 0; }, "the plate rests on both games");
    THEN("it flew on from the guest's hands on the host, and the guest showed its own fall until the host's rest")
    {
        CHECK(hostPlate.Flight.Body.Position.x > cLetGo.x);
        CHECK(hostPlate.Flight.Body.Position.z == kTable);
        CHECK(guestPlate.Drives == 0);
        CHECK(glm::distance(guestPlate.Flight.Body.Position, hostPlate.Flight.Body.Position) < 0.01f);
    }
}

namespace
{
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint8_t kRagdollBodies = 3;

// The session id of aGame's actor (its own) or copy, by form id.
uint32_t SessionOf(World& aGame, uint32_t aFormId)
{
    for (const auto cEntity : aGame.view<FormIdComponent>())
        if (aGame.get<FormIdComponent>(cEntity).Id == aFormId)
        {
            if (const auto* cpLocal = aGame.try_get<LocalComponent>(cEntity))
                return cpLocal->Id;
            if (const auto* cpRemote = aGame.try_get<RemoteComponent>(cEntity))
                return cpRemote->Id;
        }
    return 0;
}
} // namespace

SCENARIO("A copy's body correction waits for the engine update that maps its bones", "[physics][ragdoll-stage]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the copy exists");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    const BodyKey cKey{.Actor = SessionOf(party.Host(), guard)};
    host.Actors.at(guard).KnockedDown = true;
    guest.Actors.at(cCopy).KnockedDown = true;
    guest.Actors.at(cCopy).GraphUpdating = GENERATE(false, true);
    auto& body = guest.Bodies[cKey];
    body.RagdollOf = cCopy;
    const auto cBefore = body.Flight.Body.Position;
    BodyFrame frame{.Key = cKey, .Position = {20.f, 30.f, 40.f}, .Knocked = true};
    party.As(party.Guest(), [&] { REQUIRE(guest.Drive(frame, cCopy)); });
    CHECK(body.Flight.Body.Position == cBefore);
    party.Frame();
    CHECK(body.Flight.Body.Position == frame.Position);
}

SCENARIO("A host NPC dies and ragdolls: its copy's body lies as the host's, bone by bone", "[physics][corpse-rest]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {20.f, 20.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest has the host's guard");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    const uint32_t cSession = SessionOf(party.Host(), guard);
    REQUIRE(cSession == SessionOf(party.Guest(), cCopy));

    // Each game's guard has its ragdoll's bodies: the host's by its form id (HostEngine keys them by session id), the
    // copy's by the session id BodyService resolves to it.
    for (uint8_t i = 0; i < kRagdollBodies; ++i)
    {
        FakeEngine::Body bone;
        bone.Flight.Body.Position = {20.f, 20.f, 40.f + 20.f * i};
        bone.RagdollOf = guard;
        host.Bodies[{.Actor = guard, .Index = i}] = bone;
        bone.RagdollOf = cCopy;
        guest.Bodies[{.Actor = cSession, .Index = i}] = bone;
    }

    WHEN("the guard dies on the host and its ragdoll falls before the copy's death replays")
    {
        party.As(party.Host(), [&] {
            host.Hit(guard, 200.f);
            for (uint8_t i = 0; i < kRagdollBodies; ++i)
                host.Knock({.Actor = guard, .Index = i}, {40.f * i, 30.f, 50.f}, {0.f, 2.f, 1.f});
        });
        const BodyKey cHead{.Actor = cSession, .Index = kRagdollBodies - 1};
        THEN("the copy's ragdoll waits for its own death, then follows and lies as the host's")
        {
            bool drivenAlive = false;
            party.Until([&] {
                drivenAlive |= !guest.IsActorDead(cCopy) && guest.Bodies.at(cHead).Drives > 0;
                return guest.IsActorDead(cCopy);
            }, "the guest's copy dies");
            CHECK_FALSE(drivenAlive);
            CHECK_FALSE(guest.Actors.at(cCopy).KnockedDown);
            party.Until([&] {
                for (uint8_t i = 0; i < kRagdollBodies; ++i)
                    if (host.Bodies.at({.Actor = guard, .Index = i}).Awake)
                        return false;
                return guest.Bodies.at(cHead).Rests > 0;
            }, "the guard's ragdoll rests on both games");
            for (uint8_t i = 0; i < kRagdollBodies; ++i)
            {
                const auto& cHostBone = host.Bodies.at({.Actor = guard, .Index = i});
                const auto& cCopyBone = guest.Bodies.at({.Actor = cSession, .Index = i});
                INFO("bone " << int(i));
                CHECK(glm::distance(cCopyBone.Flight.Body.Position, cHostBone.Flight.Body.Position) < 0.01f);
                CHECK(cCopyBone.Held);
            }
            guest.Bodies.at(cHead).Flight.Body.Position.x += 20.f;
            party.Frame();
            CHECK(glm::distance(guest.Bodies.at(cHead).Flight.Body.Position,
                                host.Bodies.at({.Actor = guard, .Index = kRagdollBodies - 1}).Flight.Body.Position) < 0.01f);
        }
    }
}

SCENARIO("A host NPC is knocked down alive: its copy goes down with it and lies as the host's, bone by bone", "[physics]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {20.f, 20.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest has the host's guard");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    const uint32_t cSession = SessionOf(party.Host(), guard);
    for (uint8_t i = 0; i < kRagdollBodies; ++i)
    {
        FakeEngine::Body bone;
        bone.Flight.Body.Position = {20.f, 20.f, 40.f + 20.f * i};
        bone.RagdollOf = guard;
        host.Bodies[{.Actor = guard, .Index = i}] = bone;
        bone.RagdollOf = cCopy;
        guest.Bodies[{.Actor = cSession, .Index = i}] = bone;
    }

    party.As(party.Host(), [&] {
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            host.Knock({.Actor = guard, .Index = i}, {40.f * i, 30.f, 50.f}, {0.f, 2.f, 1.f});
    });
    const BodyKey cHead{.Actor = cSession, .Index = kRagdollBodies - 1};
    party.Until([&] { return guest.Actors.at(cCopy).KnockedDown; }, "the copy goes down with the host's guard");
    party.Until([&] {
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            if (host.Bodies.at({.Actor = guard, .Index = i}).Awake)
                return false;
        return guest.Bodies.at(cHead).Rests > 0;
    }, "the guard's ragdoll rests on both games");
    CHECK_FALSE(guest.IsActorDead(cCopy));
    for (uint8_t i = 0; i < kRagdollBodies; ++i)
    {
        INFO("bone " << int(i));
        CHECK(glm::distance(guest.Bodies.at({.Actor = cSession, .Index = i}).Flight.Body.Position,
                            host.Bodies.at({.Actor = guard, .Index = i}).Flight.Body.Position) < 0.01f);
    }
}

SCENARIO("A living ragdoll's reliable rest survives lost moving frames and is held until its owner gets up", "[physics][held-knock-rest]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the copy exists");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    const uint32_t cSession = SessionOf(party.Host(), guard);
    host.Actors.at(guard).KnockedDown = true;
    FakeEngine::Body bone;
    bone.RagdollOf = cCopy;
    guest.Bodies[{.Actor = cSession}] = bone;
    NotifyBodyRests rest;
    rest.Tick = party.Host().GetTransport().GetTick();
    rest.Rests = {{.Key = {.Actor = cSession}, .Position = {12.1234f, 2.f, 3.f}, .Knocked = true}};
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(rest); });
    party.Until([&] { return guest.Actors.at(cCopy).KnockedDown && guest.Bodies.at({.Actor = cSession}).Drives > 0; },
                "the reliable rest takes the copy down without a moving frame");
    CHECK(guest.Bodies.at({.Actor = cSession}).Flight.Body.Position == rest.Rests[0].Position);
    guest.Bodies.at({.Actor = cSession}).Flight.Body.Position.x += 20.f;
    party.Frame();
    CHECK(guest.Bodies.at({.Actor = cSession}).Flight.Body.Position == rest.Rests[0].Position);
    // Native action replay is covered in the duo; this flow checks the service's owner-end control.
    party.As(party.Guest(), [&] { party.Guest().ctx().at<BodyService>().EndRagdoll(cSession, rest.Tick); });
    const auto cDrives = guest.Bodies.at({.Actor = cSession}).Drives;
    for (int i = 0; i < 10; ++i)
        party.Frame();
    CHECK(guest.Bodies.at({.Actor = cSession}).Drives == cDrives);
}

SCENARIO("A host NPC knocked down hard: its copy stays on its own ragdoll while it lies", "[physics]")
{
    // Skyrim pins a ragdolled actor's skeleton to its reference: a copy's reference away from its ragdoll shows the
    // body that far from where its bodies lie (knockdown-lies-as-host, physics-sync.md "A copy's ragdoll, steered").
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {20.f, 20.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest has the host's guard");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    const uint32_t cSession = SessionOf(party.Host(), guard);
    for (uint8_t i = 0; i < kRagdollBodies; ++i)
    {
        FakeEngine::Body bone;
        bone.Flight.Body.Position = {20.f, 20.f, 40.f + 20.f * i};
        bone.RagdollOf = guard;
        host.Bodies[{.Actor = guard, .Index = i}] = bone;
        bone.RagdollOf = cCopy;
        guest.Bodies[{.Actor = cSession, .Index = i}] = bone;
    }

    // It stands a moment first: its copy has its movement to play, as in a game.
    const auto cStood = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    party.Until([&] { return std::chrono::steady_clock::now() > cStood; }, "the guard stands a moment");
    // Its knock puts it on its ragdoll (feet to pelvis) before its bodies simulate and send (knock state 2): its lying
    // point can reach the copy before the copy's knock (knockdown-smoke).
    party.As(party.Host(), [&] {
        host.Actors.at(guard).KnockedDown = true;
        std::copy_n(&host.Bodies.at({.Actor = guard, .Index = 0}).Flight.Body.Position.x, 3, host.Actors.at(guard).Position);
    });
    const auto cKeyframed = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    party.Until([&] { return std::chrono::steady_clock::now() > cKeyframed; }, "the guard lies on its ragdoll, keyframed");
    // A shout's push: the whole body thrown back and up, spinning, sliding to a stop (its spin halved at each bounce).
    party.As(party.Host(), [&] {
        host.Actors.at(guard).KnockedDown = false;
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            host.Knock({.Actor = guard, .Index = i}, {150.f, 0.f, 80.f}, {0.f, 0.f, 8.f});
    });
    const BodyKey cRoot{.Actor = cSession, .Index = 0};
    float worst = 0.f, worstTurn = 0.f, risen = 0.f;
    const auto cWatch = [&](const std::function<bool()>& acDone) {
        return [&, acDone] {
            // Standing until it falls: never placed where its owner lies (its pelvis).
            if (!guest.Actors.at(cCopy).KnockedDown)
                risen = std::max(risen, guest.Actors.at(cCopy).Position[2]);
            if (guest.Actors.at(cCopy).KnockedDown && guest.Bodies.at(cRoot).Drives > 0)
            {
                const float* cpAt = guest.Actors.at(cCopy).Position;
                worst = std::max(worst, glm::distance(glm::vec3(cpAt[0], cpAt[1], cpAt[2]), guest.Bodies.at(cRoot).Flight.Body.Position));
                const float cTurn = guest.Actors.at(cCopy).Rotation[2] - FakeEngine::HeadingOf(guest.Bodies.at(cRoot).Flight.Body.Rotation);
                worstTurn = std::max(worstTurn, std::abs(std::remainder(cTurn, 6.28318531f)));
            }
            return acDone();
        };
    };
    party.Until(cWatch([&] { return guest.Bodies.at(cRoot).Drives > 0; }), "the copy's ragdoll follows the host's");
    party.Until(cWatch([&] { return !host.Bodies.at({.Actor = guard, .Index = 0}).Awake; }), "the host's ragdoll rests");
    party.Until(cWatch([&] { return guest.Bodies.at(cRoot).Rests > 0; }), "the copy's ragdoll rests");
    INFO("the copy's reference strayed up to " << worst << " units and " << worstTurn << " rad from its root body");
    CHECK(worst < 1.f);
    CHECK(worstTurn < 0.03f);
    THEN("it stands until it falls, never rising toward its owner's pelvis first (knockdown-smoke)")
        CHECK(risen < 1.f);
    THEN("it is moved where the engine moves an actor, so its skeleton never snaps (knockdown-smoke)")
    {
        INFO(guest.Actors.at(cCopy).Snaps << " snaps, the worst " << guest.Actors.at(cCopy).WorstSnap << " units");
        CHECK(guest.Actors.at(cCopy).Snaps == 0);
    }
}

SCENARIO("A guest's player dies and ragdolls: the host's and the other guest's copies lie as its body, bone by bone", "[physics][death]")
{
    Party party(2, Plugins);
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Agree("all players are present");
    const uint32_t cSession = SessionOf(party.Guest(), FakeEngine::kPlayer);
    REQUIRE(cSession != 0);
    // Keep recovery out of this corpse-pose check: under loss the games can simulate ten seconds before a packet arrives.
    party.As(party.Host(), [&] {
        auto& host = Party::Engine(party.Host());
        host.Fights(FakeEngine::kPlayer, host.CopiesOf()[0]);
    });
    party.Frame();
    // The guest's player's ragdoll by its form id (GuestEngine keys it by index), each copy's by the session id.
    const std::vector<World*> cOthers{&party.Host(), &party.Guest(1)};
    // Each other game's copy of the guest's player now (its death spawns it again).
    const auto cCopyOn = [&](World& aGame) {
        for (const uint32_t cId : Party::Engine(aGame).CopiesOf())
            if (SessionOf(aGame, cId) == cSession)
                return cId;
        return 0u;
    };
    party.Until([&] { return std::ranges::all_of(cOthers, [&](World* apGame) { return cCopyOn(*apGame) != 0; }); },
                "each other game has the guest's copy");
    for (uint8_t i = 0; i < kRagdollBodies; ++i)
    {
        FakeEngine::Body bone;
        bone.Flight.Body.Position = {20.f, 20.f, 40.f + 20.f * i};
        bone.RagdollOf = FakeEngine::kPlayer;
        guest.Bodies[{.Actor = FakeEngine::kPlayer, .Index = i}] = bone;
        for (World* pGame : cOthers)
        {
            bone.RagdollOf = cCopyOn(*pGame);
            Party::Engine(*pGame).Bodies[{.Actor = cSession, .Index = i}] = bone;
        }
    }

    party.As(party.Guest(), [&] {
        guest.Hit(FakeEngine::kPlayer, 200.f);
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            guest.Knock({.Actor = FakeEngine::kPlayer, .Index = i}, {40.f * i, 30.f, 50.f}, {0.f, 2.f, 1.f});
    });
    const BodyKey cHead{.Actor = cSession, .Index = kRagdollBodies - 1};
    party.Until([&] {
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            if (guest.Bodies.at({.Actor = FakeEngine::kPlayer, .Index = i}).Awake)
                return false;
        return std::ranges::all_of(cOthers, [&](World* apGame) { return Party::Engine(*apGame).Bodies.at(cHead).Rests > 0; });
    }, "the guest's ragdoll rests on every game");
    for (World* pGame : cOthers)
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
        {
            const auto& cOwnBone = guest.Bodies.at({.Actor = FakeEngine::kPlayer, .Index = i});
            const auto& cCopyBone = Party::Engine(*pGame).Bodies.at({.Actor = cSession, .Index = i});
            INFO((pGame == &party.Host() ? "the host's copy" : "the other guest's copy") << ", bone " << int(i));
            CHECK(Party::Engine(*pGame).IsActorDead(cCopyOn(*pGame)));
            CHECK(cCopyBone.Drives > 0);
            CHECK(glm::distance(cCopyBone.Flight.Body.Position, cOwnBone.Flight.Body.Position) < 0.01f);
        }
}
