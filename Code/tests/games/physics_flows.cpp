// Physics sync across games (docs/design/physics-sync.md): the host's game simulates every body, a guest's shows the
// host's: held keyframed, following the host's flight a little behind, resting exactly where the host's rests.
#include "Party.h"

#include <Components/RemoteComponent.h>
#include <Services/BodyService.h>
#include <Services/Guest/GuestHands.h>
#include <Messages/Relayed.h>
#include <Physics/BodyPrediction.h>
#include <Bridges/PhysicsBodies.h>

#include <chrono>
#include <format>
#include <thread>

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

// Lydia, placed in the plugins beside the table (the host's NPC, a copy on each guest's game).
constexpr uint32_t kLydia = 0x0001A100;
constexpr uint32_t kLydiaBase = 0x000A2C8E;
const glm::vec3 kLydiaFeet{300.f, 200.f, 0.f};

void PluginsWithLydia(FakeEngine& aEngine)
{
    Plugins(aEngine);
    aEngine.PlaceNpc(kLydia, kLydiaBase, kSmoke, kLydiaFeet);
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

SCENARIO("A flying body moves evenly on the guest's game though the session's time is read unevenly", "[physics][copy-clock]")
{
    // S10 for bodies: the engine's frames are steady and Havok steps by them, while our update lands 0 or 11 ms into
    // each, where the session's time is read. The guest's bucket must step as the host's flight does each frame.
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    constexpr double cFrameMs = 1000.0 / 60.0;
    host.FrameMs = guest.FrameMs = cFrameMs;
    party.Frame();
    constexpr float cSpeed = 300.f;
    party.As(party.Host(), [&] { host.Knock(kBucket, {cSpeed, 0.f, 400.f}, {}); });

    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    std::vector<std::pair<int, float>> xs; // the guest's bucket each frame (frame, x) while it flies at cSpeed across
    const auto cStart = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 150; ++frame)
    {
        std::this_thread::sleep_until(cStart + std::chrono::duration<double, std::milli>(frame * cFrameMs));
        std::this_thread::sleep_for(std::chrono::milliseconds(frame % 2 ? 11 : 0));
        party.Frame();
        if (guestBucket.Drives > 0 && guestBucket.Flight.Body.Position.z > 1.f)
            xs.emplace_back(frame, guestBucket.Flight.Body.Position.x);
    }
    REQUIRE(xs.size() > 30);

    const float cStep = cSpeed * static_cast<float>(cFrameMs / 1000.0);
    int uneven = 0;
    std::string steps;
    for (size_t i = 8; i + 1 < xs.size(); ++i) // past the first frame's blend from where it stood
    {
        if (xs[i + 1].first != xs[i].first + 1)
            continue; // on the floor between (its bounce)
        const float cMove = xs[i + 1].second - xs[i].second;
        uneven += std::abs(cMove - cStep) > cStep / 3.f;
        if (steps.size() < 200)
            steps += std::format("{:.1f} ", cMove);
    }
    INFO(uneven << " of " << xs.size() - 9 << " frames a third off the host's steady " << cStep << " units; steps " << steps);
    CHECK(uneven == 0);
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

SCENARIO("A guest's player carries the plate and lets go: the host shows it in its hands, then it flies on", "[physics]")
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
    THEN("the host's is keyframed on the holder's frames (its alone: the host's grab refuses it), a little behind")
    {
        CHECK(hostPlate.Held);
        const float cBehind = glm::length(cCarry) * (BodyService::kDelayMs / 1000.f + 2.f * longestFrame) + 5.f;
        INFO("longest frame " << longestFrame << " s");
        INFO("host tick " << party.Host().GetTransport().GetTick() << ", guest tick " << party.Guest().GetTransport().GetTick()
             << ", RTT " << party.Guest().GetTransport().GetRoundTripTime() << ", host x " << hostPlate.Flight.Body.Position.x << ", guest x " << guestPlate.Flight.Body.Position.x);
        CHECK(glm::distance(hostPlate.Flight.Body.Position, guestPlate.Flight.Body.Position) < cBehind);
    }

    Party::Engine(party.Guest()).Grabbed.reset();
    glm::vec3 letGo{};
    party.Until([&] { letGo = hostPlate.Flight.Body.Position; return !hostPlate.Held; }, "the host's plate is let go");
    const glm::vec3 cLetGo = letGo;
    party.Until([&] { return !hostPlate.Awake && guestPlate.Rests > 0; }, "the plate rests on both games");
    THEN("it flew on from where the host had it, and the guest showed its own fall until the host's rest")
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
        AND_THEN("once the copy is gone, its held rest drives nothing (BodyService's copy index lasts one update)")
        {
            party.Until([&] { return guest.Bodies.at(cHead).Rests > 0; }, "the guard's ragdoll rests");
            World& world = party.Guest();
            for (const auto cEntity : world.view<RemoteComponent>())
                if (world.get<RemoteComponent>(cEntity).Id == cSession)
                    world.remove<RemoteComponent>(cEntity);
            const size_t cDrives = guest.Bodies.at(cHead).Drives;
            const size_t cRests = guest.Bodies.at(cHead).Rests;
            party.Frame();
            party.Frame();
            CHECK(guest.Bodies.at(cHead).Drives == cDrives);
            CHECK(guest.Bodies.at(cHead).Rests == cRests);
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

SCENARIO("A host NPC gets up from a knockdown: its copy's ragdoll follows its owner's until both stand", "[physics][rising]")
{
    // Skyrim (duo, knockdown-lies-as-host, 2026-11-11, 2 of 7 runs): the copy's head stayed 52 units above the floor for
    // 2.5 s of its get-up while its owner's rose (the same place each time; its heading and clip were its owner's).
    // Getting up, a ragdoll's bodies are still simulated (58291 only drives them toward the animation), so what the
    // room does to them is each game's own (physics.md, Getting up).
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
            host.Knock({.Actor = guard, .Index = i}, {10.f * i, 30.f, 50.f}, {0.f, 2.f, 1.f});
    });
    const BodyKey cOwnersHead{.Actor = guard, .Index = kRagdollBodies - 1};
    const BodyKey cHead{.Actor = cSession, .Index = kRagdollBodies - 1};
    party.Until([&] { return !host.Bodies.at(cOwnersHead).Awake && guest.Bodies.at(cHead).Rests > 0; }, "the guard lies still on both games");

    // The guest's room holds the copy's head where it lies.
    guest.Bodies.at(cHead).Caught = true;
    const glm::vec3 cLay = guest.Bodies.at(cHead).Flight.Body.Position;
    ActionEvent getUp;
    getUp.ActionId = FakeEngine::kActionGetUp;
    getUp.EventName = "GetUpBegin";
    party.As(party.Host(), [&] {
        host.StandUp(guard);
        (void)host.PerformAnimation(guard, getUp);
    });
    REQUIRE(host.Actors.at(guard).Rising > 0);
    bool rose = false;
    float apart = 0.f;
    party.Until([&] {
        rose |= guest.Actors.at(cCopy).Rising > 0;
        // While both get up: the copy's head against its owner's (the copy plays a moment behind).
        if (host.Actors.at(guard).Rising && guest.Actors.at(cCopy).Rising)
            apart = glm::distance(guest.Bodies.at(cHead).Flight.Body.Position, host.Bodies.at(cOwnersHead).Flight.Body.Position);
        return rose && !host.Actors.at(guard).Rising && !guest.Actors.at(cCopy).Rising && !guest.Actors.at(cCopy).KnockedDown;
    }, "the guard and its copy stand");
    const glm::vec3 cOwners = host.Bodies.at(cOwnersHead).Flight.Body.Position;
    REQUIRE(glm::distance(cOwners, cLay) > 20.f); // the owner's head rose
    INFO("the copy's head ended " << glm::distance(guest.Bodies.at(cHead).Flight.Body.Position, cOwners) << " units from its owner's, "
                                  << apart << " while both rose");
    // The copy stands a playback delay before its owner's last frames are due (its get-up is its graph's, sooner than
    // its bodies'): it ends a few frames' travel behind, not where its room held it (85 units).
    THEN("the copy's head rose with its owner's, whatever its own room held")
        CHECK(glm::distance(guest.Bodies.at(cHead).Flight.Body.Position, cOwners) < 15.f);
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
    // Skyrim (duo, kill-move-kills-guest, 2026-11-11, 14 of 14 runs): every dead copy's 18 bodies stayed awake at 3 to
    // 8 units/s within 0.8 units of their rests. Its owner's rest was steered to every step with no end, and a
    // steered body never sleeps. At its rest a copy's body is left alone.
    THEN("at their rests the copies' bodies are left alone")
    {
        std::vector<size_t> drives;
        for (World* pGame : cOthers)
            drives.push_back(Party::Engine(*pGame).Bodies.at(cHead).Drives);
        // Steered there for half a second, put there, then no more.
        for (int i = 0; i < 2 * BodyPrediction::kRestSettleSteps; ++i)
            party.Frame();
        drives.clear();
        for (World* pGame : cOthers)
            drives.push_back(Party::Engine(*pGame).Bodies.at(cHead).Drives);
        for (int i = 0; i < 30; ++i)
            party.Frame();
        for (size_t i = 0; i < cOthers.size(); ++i)
            CHECK(Party::Engine(*cOthers[i]).Bodies.at(cHead).Drives == drives[i]);
    }
}

SCENARIO("A guest's player holds the plate: the host's player cannot grab it, nor can another guest's", "[physics]")
{
    // host-cannot-take-guest-hold (2026-11-02): the host's game steered its own body after the guest's hands,
    // simulated, so the host's player grabbed it out of them.
    Party party(2, Plugins);
    auto& guest = Party::Engine(party.Guest(0));
    auto& host = Party::Engine(party.Host());
    auto& other = Party::Engine(party.Guest(1));
    guest.Grabbed = kPlate;
    const glm::vec3 cHands{140.f, 260.f, 150.f};
    party.Until([&] {
        guest.HoldTo(kPlate, cHands);
        BodyOf(party, party.Guest(0), kPlate).Flight.Body.LinearVelocity = {0.f, 0.f, 1.f};
        return BodyOf(party, party.Host(), kPlate).Drives > 0 && BodyOf(party, party.Guest(1), kPlate).Drives > 0;
    }, "every other game shows the guest's hold");
    THEN("neither the host's player nor the other guest's can grab it, nor take it (activation refused)")
    {
        CHECK_FALSE(host.TryGrab(kPlate));
        CHECK_FALSE(other.TryGrab(kPlate));
        CHECK(guest.Grabbed == kPlate);
        CHECK(party.Host().ctx().at<BodyService>().HeldElsewhere(FakeEngine::ReferenceOf(kPlate)));
        CHECK(party.Guest(1).ctx().at<BodyService>().HeldElsewhere(FakeEngine::ReferenceOf(kPlate)));
        CHECK_FALSE(party.Guest(0).ctx().at<BodyService>().HeldElsewhere(FakeEngine::ReferenceOf(kPlate)));
    }
    guest.Grabbed.reset();
    party.Until([&] { return !party.Host().ctx().at<BodyService>().HeldElsewhere(FakeEngine::ReferenceOf(kPlate)) &&
                             !party.Guest(1).ctx().at<BodyService>().HeldElsewhere(FakeEngine::ReferenceOf(kPlate)); },
                "let go, anyone may take it again");
}

SCENARIO("A guest's player lowers the plate onto a host NPC's head: its copy stops it, as every other game shows",
         "[physics]")
{
    // S1 (playtests/2026-10-31-fabien.md, guest-bucket-on-npc-head): a held object went through heads. The hold is
    // the holder's alone (2026-11-02): its own game's physics stops it on her copy (stepped, so its capsule pushes:
    // actor-movement.md, "Copies step"), and every other game shows its frames.
    Party party(2, PluginsWithLydia);
    auto& guest = Party::Engine(party.Guest(0));
    auto& guestPlate = BodyOf(party, party.Guest(0), kPlate);
    auto& hostPlate = BodyOf(party, party.Host(), kPlate);
    auto& otherPlate = BodyOf(party, party.Guest(1), kPlate);
    party.Until([&] { return guest.Actors.at(kLydia).Movements > 0; }, "the guest's game moves her copy by its owner's movement");
    guest.Grabbed = kPlate;
    const glm::vec3 cAbove = kLydiaFeet + glm::vec3(0.f, 0.f, 200.f);
    guestPlate.Flight.Body.Position = cAbove;
    const auto cBegan = std::chrono::steady_clock::now();
    const auto cHeld = [&] { return std::chrono::duration<float>(std::chrono::steady_clock::now() - cBegan).count(); };
    // The guest's hands: over her head for half a second, then down through it at 100 units/s.
    const auto cMove = [&] {
        const float cLowered = std::max(0.f, cHeld() - 0.5f) * 100.f;
        guest.HoldTo(kPlate, cAbove - glm::vec3(0.f, 0.f, std::min(cLowered, 180.f)));
        guestPlate.Flight.Body.LinearVelocity = {};
    };
    float guestLowest = 1e9f, hostLowest = 1e9f, otherLowest = 1e9f;
    const auto cOverHer = [](const FakeEngine::Body& acBody) {
        return glm::distance(glm::vec2(acBody.Flight.Body.Position), glm::vec2(kLydiaFeet)) < FakeEngine::kCapsuleRadius;
    };
    const auto cWatch = [&] {
        if (cOverHer(guestPlate))
            guestLowest = std::min(guestLowest, guestPlate.Flight.Body.Position.z - kLydiaFeet.z);
        if (cOverHer(hostPlate) && hostPlate.Drives)
            hostLowest = std::min(hostLowest, hostPlate.Flight.Body.Position.z - kLydiaFeet.z);
        if (cOverHer(otherPlate) && otherPlate.Drives)
            otherLowest = std::min(otherLowest, otherPlate.Flight.Body.Position.z - kLydiaFeet.z);
    };
    party.Until([&] { cMove(); cWatch(); return cHeld() > 2.8f; }, "the guest's player lowers the plate through her head", {}, nullptr,
                std::chrono::seconds{5});
    THEN("the plate sits on her copy's head in the guest's game, and the host and the other guest show it there")
    {
        INFO("guest's lowest " << guestLowest << ", host's " << hostLowest << " (drives " << hostPlate.Drives << "), other guest's " << otherLowest);
        CHECK(guestLowest >= FakeEngine::kCapsuleTop - 1.f);
        CHECK(hostLowest >= FakeEngine::kCapsuleTop - 1.f);
        CHECK(hostLowest < FakeEngine::kCapsuleTop + 10.f);
        CHECK(otherLowest >= FakeEngine::kCapsuleTop - 1.f);
    }
    guest.Grabbed.reset();
    party.Until([&] { return !hostPlate.Held; }, "the host's plate is let go");
}

SCENARIO("A guest's player presses a held plate against a host NPC's copy: her copy's capsule keeps it out", "[physics]")
{
    // guest-holds-into-npc (2026-11-02 playtest): the guest held a bucket against Lydia and it sank into her copy;
    // a copy only placed (ForcePosition) never steps its character controller, the only thing that pushes bodies.
    Party party(1, PluginsWithLydia);
    auto& guest = Party::Engine(party.Guest());
    const auto& cHerCopy = guest.Actors.at(kLydia);
    party.Until([&] { return cHerCopy.Copy && cHerCopy.Movements > 0; },
                "the guest's game moves her copy by its owner's movement");
    guest.Grabbed = kPlate;
    // The guest's hands at her chest, on her axis: the grab's spring pulls the plate into her.
    const glm::vec3 cIntoHer = kLydiaFeet + glm::vec3(0.f, 0.f, 100.f);
    const auto cBegan = std::chrono::steady_clock::now();
    float nearest = 1e9f;
    party.Until([&] {
        guest.HoldTo(kPlate, cIntoHer);
        const glm::vec3 cAt = guest.Bodies.at(kPlate).Flight.Body.Position;
        const glm::vec3 cFeet(cHerCopy.Position[0], cHerCopy.Position[1], cHerCopy.Position[2]);
        nearest = std::min(nearest, glm::distance(glm::vec2(cAt), glm::vec2(cFeet)));
        return std::chrono::steady_clock::now() - cBegan > std::chrono::seconds(1);
    }, "the guest's player holds the plate against her copy for a second", {}, nullptr, std::chrono::seconds{5});
    THEN("the plate never enters her copy's capsule: the copy is stepped onto its owner's place, not placed")
    {
        INFO("nearest " << nearest << " off her axis, her copy stepped " << cHerCopy.Stepped);
        CHECK(cHerCopy.Copy);
        CHECK(cHerCopy.Stepped);
        CHECK(nearest >= FakeEngine::kCapsuleRadius - 0.01f);
    }
    guest.Grabbed.reset();
}

SCENARIO("The host's player holds a bucket still: Havok sleeps it, every guest still shows it in the hand", "[physics]")
{
    // host-holds-into-npc (2026-11-02 report): pressed against Lydia, the host's bucket slept and was sent as a rest;
    // the guest's, a rest in mid-air, fell, was put back by the host's pose, and fell again, over and over.
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    const glm::vec3 cInHand{100.f, 260.f, 200.f};
    host.Grabbed = kBucket;
    hostBucket.Flight.Floor = kTable;
    hostBucket.Flight.Body.Position = cInHand;
    hostBucket.Awake = true;
    party.Until([&] { return guestBucket.Drives > 0; }, "the guest plays the host's held bucket");
    // Pressed still against something: the engine sleeps it while the hand still holds it.
    hostBucket.Awake = false;
    const auto cBegan = std::chrono::steady_clock::now();
    party.Until([&] { return std::chrono::steady_clock::now() - cBegan > std::chrono::milliseconds(800); },
                "the host's player holds it still", {}, nullptr, std::chrono::seconds{5});
    THEN("the guest never gets it as a rest while it is held, and shows it in the host's hand")
    {
        INFO("guest's bucket at " << guestBucket.Flight.Body.Position.z << ", rests " << guestBucket.Rests << ", drives "
             << guestBucket.Drives);
        CHECK(guestBucket.Rests == 0);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, cInHand) < 1.f);
    }
    host.Grabbed.reset();
}

SCENARIO("The host's engine moves a knocked bucket's 3D with it: the host's own physics is left alone", "[physics]")
{
    // A node write on a body the engine already followed re-places it and loses its contacts: a dropped bucket sank
    // 21 units into a head on the host, never in a game without co-op (physics-sync.md, "Matches a game without co-op").
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    hostBucket.Flight.Floor = kTable;
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Knock(kBucket, {120.f, 0.f, 150.f}, {0.f, 6.f, 0.f}); });
    party.Until([&] { return !hostBucket.Awake && BodyOf(party, party.Guest(), kBucket).Rests > 0; }, "the bucket rests on both games");
    THEN("the host never wrote its node, so never re-placed it")
    {
        CHECK(hostBucket.NodeWrites == 0);
        CHECK(hostBucket.Replaced == 0);
    }
}

SCENARIO("The host's engine leaves a knocked bucket's 3D behind: the host writes it, after a small knock too", "[physics]")
{
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    hostBucket.NodeLeftBehind = true;
    hostBucket.Flight.Floor = kTable;
    const glm::vec3 cStart = hostBucket.Flight.Body.Position;
    // A nudge along the table: it slides a few units (FakeFlight's friction), its 3D left where it was by the engine.
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Knock(kBucket, {60.f, 0.f, 0.f}); });
    party.Until([&] { return !hostBucket.Awake && BodyOf(party, party.Guest(), kBucket).Rests > 0; }, "the bucket rests on both games");
    THEN("its 3D is where its body rests: the host wrote it, without re-placing a followed body")
    {
        INFO("slid " << glm::distance(hostBucket.Flight.Body.Position, cStart) << " units");
        CHECK(glm::distance(hostBucket.Flight.Body.Position, cStart) > 2.f);
        CHECK(glm::distance(hostBucket.Flight.Body.Position, cStart) < 32.f);
        CHECK(glm::distance(hostBucket.NodePosition, hostBucket.Flight.Body.Position) < 1.f);
        CHECK(hostBucket.NodeWrites > 0);
        CHECK(hostBucket.Replaced == 0);
    }
}

SCENARIO("The host's engine leaves a spinning bucket's 3D turned as it was: the host writes its turn", "[physics]")
{
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    hostBucket.NodeLeftBehind = true;
    hostBucket.Flight.Floor = kTable;
    // It spins where it stands (FakeFlight stops its spin when it stops sliding): a slow slide, a fast spin.
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Knock(kBucket, {20.f, 0.f, 0.f}, {0.f, 0.f, 8.f}); });
    party.Until([&] { return !hostBucket.Awake; }, "the bucket rests on the host");
    THEN("its 3D is turned as its body")
    {
        const auto cApart = BodyPrediction::Between({.Position = hostBucket.NodePosition, .Rotation = hostBucket.NodeRotation},
                                                    {.Position = hostBucket.Flight.Body.Position, .Rotation = hostBucket.Flight.Body.Rotation});
        INFO("apart " << cApart.Distance << " units, " << cApart.Angle << " rad");
        CHECK(cApart.Angle < 0.01f);
        CHECK(cApart.Distance < 1.f);
        CHECK(hostBucket.Replaced == 0);
    }
}

SCENARIO("An observer plays a held reference between frames, clear of the renderer's scene reads", "[physics][body-scene-phase]")
{
    Party party(1, Plugins);
    auto& holder = Party::Engine(party.Host());
    auto& held = BodyOf(party, party.Host(), kBucket);
    auto& watched = BodyOf(party, party.Guest(), kBucket);
    holder.Grabbed = kBucket;
    holder.Knock(kBucket, {80.f, 0.f, 0.f});
    held.Flight.Floor = kTable;
    const auto cBegan = std::chrono::steady_clock::now();
    const auto cStart = held.Flight.Body.Position;
    party.Until([&] {
        const float cElapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - cBegan).count();
        held.Flight.Body.Position = cStart + glm::vec3(80.f * cElapsed, 0.f, 0.f);
        held.Flight.Body.LinearVelocity = {80.f, 0.f, 0.f};
        return watched.Drives > 5 && cElapsed > 0.3f;
    }, "the observer sees the holder move the bucket");
    REQUIRE(watched.Held);
    CHECK(watched.NodePosition.x > cStart.x);
    CHECK(watched.SceneWritesDuringJobs == 0);
    holder.Grabbed.reset();
    party.Until([&] { return !held.Awake && watched.Rests > 0; }, "release settles on both games");
    CHECK(glm::distance(watched.NodePosition, held.Flight.Body.Position) < 0.01f);
    CHECK(watched.SceneWritesDuringJobs == 0);
}

SCENARIO("A body's left-behind scene node is repaired between frames through its rest", "[physics][body-scene-phase]")
{
    Party party(1, Plugins);
    auto& held = BodyOf(party, party.Host(), kBucket);
    auto& watched = BodyOf(party, party.Guest(), kBucket);
    held.NodeLeftBehind = true;
    held.Flight.Floor = kTable;
    Party::Engine(party.Host()).Knock(kBucket, {60.f, 0.f, 0.f});
    party.Until([&] { return !held.Awake && watched.Rests > 0; }, "the repaired reference settles on both games");
    REQUIRE(held.NodeWrites > 0);
    CHECK(glm::distance(held.NodePosition, held.Flight.Body.Position) < PhysicsBodies::kLeftBehind);
    CHECK(held.SceneWritesDuringJobs == 0);
    CHECK(watched.SceneWritesDuringJobs == 0);
}

SCENARIO("A host's recorded rest places the observer's reference between frames", "[physics][body-scene-phase][world-rest-phase]")
{
    Party party(1, Plugins);
    auto& watched = BodyOf(party, party.Guest(), kBucket);
    NotifyObjectChanges changes;
    changes.Cell = GameId(0, kSmoke);
    const float cPose[6]{320.f, 240.f, kTable, 0.f, 0.f, 0.5f};
    ObjectChange change{.Type = ObjectChange::Kind::kMove, .Object = kBucket.Object};
    change.Data.assign(reinterpret_cast<const char*>(cPose), sizeof(cPose));
    changes.Changes.push_back(change);
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(changes); });
    party.Until([&] { return watched.NodePosition.x == cPose[0]; }, "the observer applies the recorded rest");
    CHECK(watched.SceneWritesDuringJobs == 0);
}

SCENARIO("A loaded body takes its rest from the host's body rest alone and not from its move row", "[physics][world-rest-phase]")
{
    // Seen 2026-11-02 for a ladle: BodyService placed the host's rest, then the move row placed it again (red-flags.md, 4).
    Party party(1, Plugins);
    auto& held = BodyOf(party, party.Host(), kBucket);
    auto& watched = BodyOf(party, party.Guest(), kBucket);
    held.Flight.Floor = kTable;
    Party::Engine(party.Host()).Knock(kBucket, {60.f, 0.f, 0.f});
    party.Until([&] { return !held.Awake && watched.Rests > 0; }, "the bucket rests on both games");
    const auto cRest = watched.NodePosition;

    // The host's change log reports the same move as a live row.
    NotifyObjectChanges changes;
    const float cPose[6]{cRest.x + 0.25f, cRest.y, cRest.z, 0.f, 0.f, 0.f};
    ObjectChange change{.Type = ObjectChange::Kind::kMove, .Object = kBucket.Object};
    change.Data.assign(reinterpret_cast<const char*>(cPose), sizeof(cPose));
    changes.Changes.push_back(change);
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(changes); });
    for (int i = 0; i < 10; ++i)
        party.Frame();
    CHECK(watched.MoveRows == 0);
    CHECK(watched.NodePosition == cRest);

    // A script's move (the console's setpos) has no body rest: its row places it.
    const float cMoved[6]{cRest.x + 50.f, cRest.y, cRest.z, 0.f, 0.f, 0.f};
    changes.Changes[0].Data.assign(reinterpret_cast<const char*>(cMoved), sizeof(cMoved));
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(changes); });
    party.Until([&] { return watched.MoveRows == 1; }, "the scripted move places the bucket");
    CHECK(watched.NodePosition.x == cMoved[0]);
}
