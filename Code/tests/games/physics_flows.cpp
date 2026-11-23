// Physics sync across games (docs/design/physics-sync.md): the host's game simulates every body, a guest's shows the
#include "Fixtures.h"
// host's: held keyframed, following the host's flight a little behind, resting exactly where the host's rests.
#include "Party.h"

#include <Components/RemoteComponent.h>
#include <Services/BodyService.h>
#include <Services/Guest/GuestBodies.h>
#include <Messages/Relayed.h>
#include <Physics/BodyPrediction.h>
#include <Physics/BodyTolerances.h>
#include <Physics/RestPlacer.h>
#include <Bridges/PhysicsBodies.h>
#include <Services/CopyRecords.h>
#include <Structs/RagdollPart.h>

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

// A game's own body: it took authority over it (the fake's class, BodyBridge::SetMoving).
bool Moves(World& aGame, const BodyKey& acKey)
{
    const auto& cClasses = Party::Engine(aGame).Classes;
    const auto it = cClasses.find(acKey);
    return it != cClasses.end() && it->second == BodyClass::kMoving;
}

// The host's stream as a guest hears it: a body nobody has, at rest or moving.
NotifyBodyStates RestOf(uint64_t aTick, const BodyKey& acKey, const glm::vec3& acAt, bool aKnocked = false, uint16_t aAuthSeq = 1)
{
    NotifyBodyStates states;
    states.Tick = aTick;
    states.States = {{.Frame = {.Key = acKey, .Position = acAt, .Knocked = aKnocked}, .Author = BodyAuthor::kNone, .AuthSeq = aAuthSeq, .AtRest = true}};
    return states;
}

NotifyBodyStates FrameOf(uint64_t aTick, const BodyKey& acKey, const glm::vec3& acAt, const glm::vec3& acVelocity = {}, uint16_t aAuthSeq = 1)
{
    NotifyBodyStates states;
    states.Tick = aTick;
    states.States = {{.Frame = {.Key = acKey, .Position = acAt, .LinearVelocity = acVelocity}, .Author = BodyAuthor::kNone, .AuthSeq = aAuthSeq}};
    return states;
}
} // namespace


SCENARIO("The host's player knocks a bucket off a table: the guest's flies with it and lies where the host's does", "[physics]")
{
    Party party(1, Plugins);
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Knock(kBucket, {120.f, 0.f, 150.f}, {0.f, 6.f, 0.f}); });

    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    party.Until([&] { return guestBucket.Drives > 0; }, "the guest's bucket follows the host's frames");
    THEN("the guest's follows the host's simulated, its mass kept (physics-sync.md, \"One rule\")") CHECK_FALSE(guestBucket.Held);

    const auto cWhere = [&] {
        return std::format("host's at z {:.1f} {}, guest's at z {:.1f}, {} drives, {} rests", hostBucket.Flight.Body.Position.z,
                           hostBucket.Awake ? "awake" : "asleep", guestBucket.Flight.Body.Position.z, guestBucket.Drives, guestBucket.Rests);
    };
    party.Until([&] { return !hostBucket.Awake && guestBucket.Rests > 0; }, "the bucket rests on both games", cWhere);
    // One that settled asleep within a settle of the rest is left there (RestPlacer: waking a pile to move it half a
    // unit costs more than the half unit).
    CHECK(glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < BodyTolerances::kSameDistance);
    CHECK(std::abs(glm::dot(guestBucket.Flight.Body.Rotation, hostBucket.Flight.Body.Rotation)) > 0.9999f);
    THEN("the plate nobody touched is left alone")
    {
        CHECK(BodyOf(party, party.Guest(), kPlate).Drives == 0);
        CHECK(!BodyOf(party, party.Guest(), kPlate).Held);
    }
}

SCENARIO("The bottles on Orgnar's bar wake without moving: the guest's player can still grab one", "[physics][woken-still]")
{
    // Playtest 2026-11-13: each wake sent the guest frames, keyframing its bottle; the grab refuses that (40555).
    Party party(1, Plugins);
    auto& hostPlate = BodyOf(party, party.Host(), kPlate);
    auto& guestPlate = BodyOf(party, party.Guest(), kPlate);
    size_t refused = 0;
    for (int wake = 0; wake < 3; ++wake)
    {
        party.As(party.Host(), [&] { Party::Engine(party.Host()).Wake(kPlate, 400); });
        party.Until([&] {
            refused += hostPlate.Awake && !Party::Engine(party.Guest()).TryGrab(kPlate);
            Party::Engine(party.Guest()).Grabbed.reset();
            return !hostPlate.Awake;
        }, "the host's plate sleeps again", [&] { return std::format("{} drives on the guest's", guestPlate.Drives); });
    }
    THEN("the guest's plate stayed its own, grabbable all along; only the host's rests placed it")
    {
        CHECK(guestPlate.Drives == 0);
        CHECK_FALSE(guestPlate.Held);
        CHECK(refused == 0);
        CHECK(glm::distance(guestPlate.Flight.Body.Position, hostPlate.Flight.Body.Position) < 0.01f);
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
    const auto rest = RestOf(party.Guest().GetFrameTick(), kBucket, {5.f, 6.f, kTable});
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(rest); });
    party.Until([&] { return body.RestAttempts > 0; }, "the engine first tries the rest");
    CHECK(body.Rests == 0);
    SECTION("a late moving frame cannot discard the deferred rest")
    {
        const auto frames = FrameOf(rest.Tick - GENERATE(0, 1), kBucket, {1.f, 2.f, kTable});
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(frames); });
        body.RestBusy = false;
        party.Until([&] { return body.Rests > 0; }, "the engine settles the retained rest when its locks are available");
        CHECK(body.Flight.Body.Position == rest.States[0].Frame.Position);
        CHECK_FALSE(body.Held);
    }
    SECTION("a newer moving frame supersedes the deferred rest")
    {
        const auto frames = FrameOf(rest.Tick + 1, kBucket, {1.f, 2.f, kTable});
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(frames); });
        body.RestBusy = false;
        party.Until([&] { return body.Drives > 0; }, "the newer motion is played");
        CHECK(body.Rests == 0);
    }
}

SCENARIO("A guest's player bumps the bucket: it slides as the guest's physics has it, and lies there in every game", "[physics][mover]")
{
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    // Two games never simulate one slide alike: the host's table holds it harder.
    hostBucket.Flight.Friction = 2.f * FakeFlight::kFriction;
    // The guest's character pushes its bucket along the table in its own game (which simulates that slide itself).
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {160.f, 0.f, 0.f});
    });
    const auto cWhere = [&] {
        return std::format("host's at x {:.1f} {}, guest's at x {:.1f} {}, {} drives, {} rests", hostBucket.Flight.Body.Position.x,
                           hostBucket.Awake ? "awake" : "asleep", guestBucket.Flight.Body.Position.x, guestBucket.Awake ? "awake" : "asleep",
                           guestBucket.Drives, guestBucket.Rests);
    };
    party.Until([&] { return !guestBucket.Awake && hostBucket.Rests > 0 && !hostBucket.Awake; }, "the bucket rests on both games", cWhere);
    for (int i = 0; i < 30; ++i)
        party.Frame();
    THEN("the guest's was never placed by another game, and the host's lies where the guest's stopped")
    {
        INFO(cWhere());
        CHECK(guestBucket.Drives == 0);
        CHECK(guestBucket.Rests == 0);
        CHECK(guestBucket.Flight.Body.Position.x > 120.f);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < 0.01f);
        CHECK_FALSE(hostBucket.Held);
    }
}

SCENARIO("A guest's player hits the bucket with its sword: the host's lies where the guest's knock put it", "[physics][mover]")
{
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    const glm::vec3 cStart = hostBucket.Flight.Body.Position;
    // The guest's swing knocks its own bucket along the table; the host's copy of that swing hits nothing.
    party.As(party.Guest(), [&] { Party::Engine(party.Guest()).Swing(kBucket, {160.f, 0.f, 0.f}); });
    const auto cWhere = [&] {
        return std::format("host's at x {:.1f} {}, guest's at x {:.1f}, {} drives, {} rests", hostBucket.Flight.Body.Position.x,
                           hostBucket.Awake ? "awake" : "asleep", guestBucket.Flight.Body.Position.x, guestBucket.Drives, guestBucket.Rests);
    };
    party.Until([&] { return !guestBucket.Awake && !hostBucket.Awake && hostBucket.Rests > 0; }, "the bucket rests where the guest's knock put it", cWhere);
    THEN("the host's moved with the hit, and lies where the guest's does; the guest's never snapped back nor placed")
    {
        INFO(cWhere());
        CHECK(glm::distance(hostBucket.Flight.Body.Position, cStart) > 10.f);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < 0.01f);
        CHECK(guestBucket.Drives == 0);
        CHECK(guestBucket.Rests == 0);
    }
}

SCENARIO("A knocked bucket stops where an animated body keeps its island awake: it rests on both games", "[physics][still]")
{
    // Havok's island sleep never came (Orgnar's body, 78910), so the host never sent the rest; a rest by its own
    // motion does (physics-sync.md, "One rule").
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    hostBucket.KeptAwake = true;
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Knock(kBucket, {0.f, 120.f, 0.f}); });
    party.Until([&] { return guestBucket.Drives > 2; }, "the guest's bucket follows the host's frames");
    const auto cWhere = [&] {
        return std::format("host's at y {:.1f} {}{}, guest's at y {:.1f}, {} rests", hostBucket.Flight.Body.Position.y,
                           hostBucket.Flight.Resting ? "stopped" : "moving", hostBucket.Awake ? " (awake)" : "", guestBucket.Flight.Body.Position.y,
                           guestBucket.Rests);
    };
    party.Until([&] { return hostBucket.Flight.Resting; }, "the host's bucket stops", cWhere);
    THEN("its rest reaches the guest though Havok keeps it awake, and the guest's lies there asleep")
    {
        party.Until([&] { return guestBucket.Rests > 0; }, "the guest places the host's rest", cWhere);
        INFO(cWhere());
        CHECK(hostBucket.Awake);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < 0.01f);
        CHECK_FALSE(guestBucket.Awake);
    }
}

SCENARIO("The host's player holds a bucket still in the air and lets go: the guest's first rest is where it lands", "[physics][still]")
{
    // Review of #56: its stillness matured in the hand, so the release was taken for a rest in midair.
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& held = BodyOf(party, party.Host(), kBucket);
    auto& watched = BodyOf(party, party.Guest(), kBucket);
    held.Flight.Floor = kTable;
    const float cAloft = held.Flight.Body.Position.z + 60.f;
    party.As(party.Host(), [&] {
        host.Grabbed = kBucket;
        held.Flight.Body.Position.z = cAloft;
        held.Flight.Resting = false;
        held.Awake = true;
    });
    const auto cBegan = std::chrono::steady_clock::now();
    party.Until([&] { return std::chrono::steady_clock::now() - cBegan > std::chrono::milliseconds(600); }, "the host's player holds it still");
    party.As(party.Host(), [&] { host.Grabbed.reset(); });
    party.Until([&] { return watched.Rests > 0; }, "the guest places a rest", [&] {
        return std::format("host's at z {:.1f} {}", held.Flight.Body.Position.z, held.Awake ? "awake" : "asleep");
    });
    THEN("that rest is on the table, not in the air where it was held")
    {
        INFO(std::format("guest's at z {:.1f}, held at {:.1f}", watched.Flight.Body.Position.z, cAloft));
        CHECK(watched.Flight.Body.Position.z < cAloft - 30.f);
    }
}

SCENARIO("A guest's player hits the bucket the host's knock still moves: it keeps its mass, so both games are knocked", "[physics]")
{
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    // The host's player knocks it along y; the guest's plays it nudged onto the host's frames, simulated (its mass).
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Knock(kBucket, {0.f, 120.f, 0.f}); });
    party.Until([&] { return guestBucket.Drives > 2; }, "the guest's bucket follows the host's frames");
    party.As(party.Guest(), [&] { Party::Engine(party.Guest()).Swing(kBucket, {160.f, 0.f, 0.f}); });
    THEN("the hit makes it the guest's: the host's lies where the guest's stopped, which the knock and the hit moved")
    {
        const auto cWhere = [&] {
            return std::format("host's at ({:.1f}, {:.1f}) {}, guest's at ({:.1f}, {:.1f}) {}, {} hits sent", hostBucket.Flight.Body.Position.x,
                               hostBucket.Flight.Body.Position.y, hostBucket.Awake ? "awake" : "asleep", guestBucket.Flight.Body.Position.x,
                               guestBucket.Flight.Body.Position.y, guestBucket.Held ? "keyframed" : "simulated",
                               Party::Engine(party.Guest()).PlayerHits.size());
        };
        party.Until([&] { return !hostBucket.Awake && !guestBucket.Awake && hostBucket.Rests > 0; }, "the bucket rests on both games", cWhere);
        INFO(cWhere());
        CHECK(glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < 0.01f);
        CHECK(hostBucket.Flight.Body.Position.x > 110.f);
        CHECK_FALSE(guestBucket.Held);
    }
}

SCENARIO("A guest's player stands beside a bucket the host knocks: it comes to rest, the guest's to hit again", "[physics][copy-step]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    // The guest's player stands at the table, 30 units from the bucket; its copy on the host is stepped there.
    party.As(party.Guest(), [&] {
        auto& player = Party::Engine(party.Guest()).Actors.at(FakeEngine::kPlayer);
        player.Position[0] = 130.f, player.Position[1] = 200.f, player.Position[2] = 0.f;
    });
    const auto cCopy = [&]() -> const FakeEngine::Actor* {
        for (const auto& [cFormId, cActor] : host.Actors)
            if (cActor.Copy && cActor.Player && cActor.Stepped && std::abs(cActor.Goal[0] - 130.f) < 1.f)
                return &cActor;
        return nullptr;
    };
    party.Until([&] { return cCopy() != nullptr; }, "the guest's copy stands at the table on the host");
    party.As(party.Host(), [&] { host.Knock(kBucket, {0.f, 60.f, 0.f}); });
    party.Until([&] { return guestBucket.Drives > 0; }, "the guest's bucket follows the host's knock", [&] {
        return std::format("host's {} at ({:.1f}, {:.1f}), {} drives", hostBucket.Awake ? "awake" : "asleep", hostBucket.Flight.Body.Position.x,
                           hostBucket.Flight.Body.Position.y, guestBucket.Drives);
    });
    THEN("the host's bucket sleeps beside the still copy, and the guest's is its own again (dynamic, hittable)")
    {
        const auto cWhere = [&] {
            return std::format("host's {} at y {:.1f}, guest's {}held, {} rests", hostBucket.Awake ? "awake" : "asleep",
                               hostBucket.Flight.Body.Position.y, guestBucket.Held ? "" : "not ", guestBucket.Rests);
        };
        party.Until([&] { return !hostBucket.Awake && guestBucket.Rests > 0 && !guestBucket.Held; }, "the bucket rests on both games", cWhere);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < 0.01f);
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
    party.Until([&] { return guestBucket.Rests > 0; }, "the guest gets the host's pose of it (its hint)");
    CHECK(glm::distance(guestBucket.Flight.Body.Position, cHost) < 0.01f);
    CHECK_FALSE(hostBucket.Awake);
    THEN("a nudge that settles in place asks nothing")
    {
        const size_t cRests = guestBucket.Rests;
        ownMoves.push_back({kBucket, guestBucket.Flight.Body.Position + glm::vec3(0.5f, 0.f, 0.f), guestBucket.Flight.Body.Rotation});
        const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(1300);
        while (std::chrono::steady_clock::now() < cUntil)
            party.Frame();
        CHECK(guestBucket.Rests == cRests);
    }
}

SCENARIO("A guest's player carries the plate and lets go: the host shows it in its hands, then where the guest's lands", "[physics][mover]")
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

    // Let go while carried: it flies on with the hand's motion (DestroyMouseSprings 40557 only activates it), and the
    // host's table holds it harder than the guest's (two games never simulate one fall alike).
    hostPlate.Flight.Friction = 2.f * FakeFlight::kFriction;
    guestPlate.Flight.Floor = kTable;
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.Grabbed.reset();
        engine.Knock(kPlate, cCarry);
    });
    party.Until([&] { return !hostPlate.Held; }, "the host's plate is let go");
    const auto cWhere = [&] {
        return std::format("host's at x {:.1f} {}, guest's at x {:.1f} {}, {} rests here", hostPlate.Flight.Body.Position.x,
                           hostPlate.Awake ? "awake" : "asleep", guestPlate.Flight.Body.Position.x, guestPlate.Awake ? "awake" : "asleep",
                           guestPlate.Rests);
    };
    party.Until([&] { return !guestPlate.Awake && hostPlate.Rests > 0 && !hostPlate.Awake; }, "the plate rests on both games", cWhere);
    for (int i = 0; i < 30; ++i)
        party.Frame();
    THEN("the guest's own fall decided where it lies: never placed by the host's, and the host's lies there")
    {
        INFO(cWhere());
        CHECK(guestPlate.Drives == 0);
        CHECK(guestPlate.Rests == 0);
        CHECK(guestPlate.Flight.Body.Position.z == kTable);
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
                CHECK(glm::distance(cCopyBone.Flight.Body.Position, cHostBone.Flight.Body.Position) < FakeEngine::kRagdollNear);
                CHECK_FALSE(cCopyBone.Held);
            }
            // Knocked off its rest (a push wakes it): taken there again.
            guest.Bodies.at(cHead).Flight.Body.Position.x += 20.f;
            guest.Bodies.at(cHead).Awake = true;
            party.Until([&] { return !guest.Bodies.at(cHead).Awake; }, "the copy's head at rest again");
            CHECK(glm::distance(guest.Bodies.at(cHead).Flight.Body.Position,
                                host.Bodies.at({.Actor = guard, .Index = kRagdollBodies - 1}).Flight.Body.Position) < FakeEngine::kRagdollNear);
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

// Skyrim (duo, death-spectate-host's RagdollTrace, 2026-11-12, 11 of 13 traces): the dead player's copy reached its
// owner's rest and slept, then woke at the rest's arrival and again 30 steps on (steered, then put there awake), and
// settled again 3 to 14 degrees off. Its bodies rest as a pile's: steered there, put there once, put to sleep, left.
SCENARIO("A dead copy at its owner's rest falls asleep there and stays asleep", "[physics][corpse-rest][ragdoll-rest]")
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
        host.Hit(guard, 200.f);
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            host.Knock({.Actor = guard, .Index = i}, {40.f * i, 30.f, 50.f}, {0.f, 2.f, 1.f});
    });
    const auto cAllAsleep = [&] {
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            if (guest.Bodies.at({.Actor = cSession, .Index = i}).Awake || host.Bodies.at({.Actor = guard, .Index = i}).Awake)
                return false;
        return guest.Bodies.at({.Actor = cSession, .Index = 0}).Rests > 0;
    };
    party.Until(cAllAsleep, "both ragdolls rest and the copy's sleeps");
    // Three seconds on: never woken, never moved, as the owner's lies.
    for (int frame = 0; frame < 180; ++frame)
    {
        party.Frame();
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
        {
            const auto& cCopyBone = guest.Bodies.at({.Actor = cSession, .Index = i});
            INFO("frame " << frame << ", bone " << int(i));
            REQUIRE_FALSE(cCopyBone.Awake);
            REQUIRE(glm::distance(cCopyBone.Flight.Body.Position, host.Bodies.at({.Actor = guard, .Index = i}).Flight.Body.Position) <
                    FakeEngine::kRagdollNear);
        }
    }
}

// A copy made after its owner's ragdoll rested (a join, a cell load, a hidden copy shown again) had no rest: the rest
// went out once, before it existed (bugs.md, "A copy that stays off its owner"). It is the actor's record part now.
SCENARIO("A guest joining beside a host corpse sees it lie as the host's", "[physics][corpse-rest][ragdoll-part]")
{
    Party party(1, Plugins, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {20.f, 20.f, 0.f}); });
    for (uint8_t i = 0; i < kRagdollBodies; ++i)
    {
        FakeEngine::Body bone;
        bone.Flight.Body.Position = {20.f, 20.f, 40.f + 20.f * i};
        bone.RagdollOf = guard;
        host.Bodies[{.Actor = guard, .Index = i}] = bone;
    }
    party.As(party.Host(), [&] {
        host.Hit(guard, 200.f);
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            host.Knock({.Actor = guard, .Index = i}, {40.f * i, 30.f, 50.f}, {0.f, 2.f, 1.f});
    });
    party.Until([&] {
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            if (host.Bodies.at({.Actor = guard, .Index = i}).Awake)
                return false;
        return true;
    }, "the host's guard lies at rest");
    for (int i = 0; i < 30; ++i)
        party.Frame();
    const uint32_t cSession = SessionOf(party.Host(), guard);
    REQUIRE(cSession != 0);
    const auto cRestsHeld = [&] {
        RagdollPart part;
        const auto* pPart = party.Host().ctx().at<CopyRecords>().Find(cSession, RecordKind::kRagdoll);
        return pPart && FromPartBytes(pPart->Bytes, part) ? part.Rests.size() : 0;
    };
    REQUIRE(cRestsHeld() == kRagdollBodies);
    // Woken again (a kick), its rest is gone until it lies still once more: a spawn meanwhile gets none.
    party.As(party.Host(), [&] { host.Knock({.Actor = guard, .Index = 0}, {40.f, 0.f, 0.f}, {}); });
    party.Until([&] { return cRestsHeld() == 0; }, "the woken corpse's rest part is cleared");
    party.Until([&] { return cRestsHeld() == kRagdollBodies; }, "it rests again and declares it");
    // The guest's engine drops its own copy's ragdoll where it stands (as Skyrim's spawning a dead actor does).
    for (uint8_t i = 0; i < kRagdollBodies; ++i)
    {
        FakeEngine::Body bone;
        bone.Flight.Body.Position = {20.f, 20.f, 40.f + 20.f * i};
        guest.Bodies[{.Actor = cSession, .Index = i}] = bone;
    }

    party.Join(party.Guest());
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && guest.IsActorDead(guest.CopiesOf(kGuard)[0]); }, "the guest has the dead guard");
    party.Until([&] {
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
        {
            const auto& cCopyBone = guest.Bodies.at({.Actor = cSession, .Index = i});
            if (cCopyBone.Awake || glm::distance(cCopyBone.Flight.Body.Position, host.Bodies.at({.Actor = guard, .Index = i}).Flight.Body.Position) >=
                                       FakeEngine::kRagdollNear)
                return false;
        }
        return true;
    }, "the guest's copy lies at the host's rest, asleep");
}

// Skyrim (duo, guest-drags-host-corpse, 2026-11-21): the guest dragged its copy of a host wolf's corpse 120 units back;
// the host's never moved and the two lay 151 units apart for good. A grab of a corpse (40555 takes a dead actor) is the
// mover's until it rests, as an object's: its frames move the host's corpse, its rest is where it lies everywhere.
SCENARIO("A guest's player drags a host NPC's corpse: it lies where the guest left it, in every game", "[physics][mover][corpse-drag]")
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
        host.Hit(guard, 200.f);
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            host.Knock({.Actor = guard, .Index = i}, {40.f * i, 30.f, 50.f}, {0.f, 2.f, 1.f});
    });
    const auto cHostBone = [&](uint8_t aIndex) -> FakeEngine::Body& { return host.Bodies.at({.Actor = guard, .Index = aIndex}); };
    const auto cCopyBone = [&](uint8_t aIndex) -> FakeEngine::Body& { return guest.Bodies.at({.Actor = cSession, .Index = aIndex}); };
    const auto cAllAsleep = [&] {
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            if (cCopyBone(i).Awake || cHostBone(i).Awake)
                return false;
        return cCopyBone(0).Rests > 0;
    };
    party.Until(cAllAsleep, "the guard lies at rest on both games");
    const glm::vec3 cLay = cHostBone(0).Flight.Body.Position;

    // Played: it passes what nobody moves in the guest's game (its owner's game settled those).
    const BodyKey cGrabbed{.Actor = cSession, .Index = 0};
    CHECK(guest.Classes.at(cGrabbed) == BodyClass::kPlayed);
    // The guest's player drags its copy 120 units along x (its own mouse spring), then lets go.
    const glm::vec3 cDrag{120.f, 0.f, 0.f};
    guest.Grabbed = BodyKey{.Actor = cSession, .Index = 0};
    std::vector<glm::vec3> starts;
    for (uint8_t i = 0; i < kRagdollBodies; ++i)
        starts.push_back(cCopyBone(i).Flight.Body.Position);
    for (int frame = 1; frame <= 60; ++frame)
    {
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
        {
            auto& bone = cCopyBone(i);
            bone.Awake = true;
            bone.Flight.Resting = false;
            bone.Flight.Body.Position = starts[i] + cDrag * (frame / 60.f);
            bone.Flight.Body.LinearVelocity = {};
        }
        party.Frame();
    }
    // The guest's own now: it meets what the guest's game moves and what nobody does, as any body it moves.
    CHECK(guest.Classes.at(cGrabbed) == BodyClass::kMoving);
    party.As(party.Guest(), [&] { guest.Grabbed.reset(); });
    const auto cWhere = [&] {
        return std::format("host's bone 0 at x {:.1f} {}, guest's at x {:.1f} {}", cHostBone(0).Flight.Body.Position.x,
                           cHostBone(0).Awake ? "awake" : "asleep", cCopyBone(0).Flight.Body.Position.x, cCopyBone(0).Awake ? "awake" : "asleep");
    };
    party.Until([&] {
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            if (cCopyBone(i).Awake || cHostBone(i).Awake ||
                glm::distance(cHostBone(i).Flight.Body.Position, cCopyBone(i).Flight.Body.Position) >= FakeEngine::kRagdollNear)
                return false;
        return true;
    }, "the host's guard lies where the guest left it", cWhere);
    for (int i = 0; i < 60; ++i)
        party.Frame();
    INFO(cWhere());
    // Where the guest's own physics left it after the let go (it slides a little): never pulled back to the host's.
    CHECK(glm::distance(cCopyBone(0).Flight.Body.Position, starts[0] + cDrag) < 5.f);
    CHECK(glm::distance(cHostBone(0).Flight.Body.Position, cLay) > 100.f);
    for (uint8_t i = 0; i < kRagdollBodies; ++i)
        CHECK(glm::distance(cHostBone(i).Flight.Body.Position, cCopyBone(i).Flight.Body.Position) < FakeEngine::kRagdollNear);
    CHECK(guest.Classes.at(cGrabbed) == BodyClass::kPlayed); // at rest: the host's again

    // The host's own physics moves it next (a blast): it goes where that puts it, the guest's rest played no more.
    const glm::vec3 cGuestRest = cHostBone(0).Flight.Body.Position;
    party.As(party.Host(), [&] {
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            host.Knock({.Actor = guard, .Index = i}, {300.f, 0.f, 0.f}, {});
    });
    party.Until([&] {
        for (uint8_t i = 0; i < kRagdollBodies; ++i)
            if (cHostBone(i).Awake)
                return false;
        return true;
    }, "the host's corpse rests where its blast put it", cWhere);
    for (int i = 0; i < 30; ++i)
        party.Frame();
    INFO(cWhere());
    CHECK(glm::distance(cHostBone(0).Flight.Body.Position, cGuestRest) > 30.f);
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
            if (host.Bodies.at({.Actor = guard, .Index = i}).Awake || guest.Bodies.at({.Actor = cSession, .Index = i}).Awake)
                return false;
        return guest.Bodies.at(cHead).Rests > 0;
    }, "the guard's ragdoll rests on both games");
    CHECK_FALSE(guest.IsActorDead(cCopy));
    for (uint8_t i = 0; i < kRagdollBodies; ++i)
    {
        INFO("bone " << int(i));
        CHECK(glm::distance(guest.Bodies.at({.Actor = cSession, .Index = i}).Flight.Body.Position,
                            host.Bodies.at({.Actor = guard, .Index = i}).Flight.Body.Position) < FakeEngine::kRagdollNear);
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
    const auto rest = RestOf(party.Guest().GetFrameTick(), {.Actor = cSession}, {12.1234f, 2.f, 3.f}, true);
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(rest); });
    party.Until([&] { return guest.Actors.at(cCopy).KnockedDown && guest.Bodies.at({.Actor = cSession}).Drives > 0; },
                "the reliable rest takes the copy down without a moving frame");
    auto& body = guest.Bodies.at({.Actor = cSession});
    party.Until([&] { return !body.Awake; }, "the copy's body asleep at the rest");
    CHECK(glm::distance(body.Flight.Body.Position, rest.States[0].Frame.Position) < FakeEngine::kRagdollNear);
    // Knocked off it (a push wakes it): taken back while its owner lies.
    body.Flight.Body.Position.x += 20.f;
    body.Awake = true;
    party.Until([&] { return !body.Awake; }, "the copy's body asleep at the rest again");
    CHECK(glm::distance(body.Flight.Body.Position, rest.States[0].Frame.Position) < FakeEngine::kRagdollNear);
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
            CHECK(glm::distance(cCopyBone.Flight.Body.Position, cOwnBone.Flight.Body.Position) < FakeEngine::kRagdollNear);
        }
    // Skyrim (duo, kill-move-kills-guest, 2026-11-11, 14 of 14 runs): every dead copy's 18 bodies stayed awake at 3 to
    // 8 units/s within 0.8 units of their rests. Its owner's rest was steered to every step with no end, and a
    // steered body never sleeps. At its rest a copy's body is left alone.
    THEN("at their rests the copies' bodies are left alone")
    {
        std::vector<size_t> drives;
        for (World* pGame : cOthers)
            drives.push_back(Party::Engine(*pGame).Bodies.at(cHead).Drives);
        // Glided there for half a second at most, put there, put to sleep, then no more.
        party.Until([&] {
            return std::ranges::all_of(cOthers, [&](World* apGame) { return !Party::Engine(*apGame).Bodies.at(cHead).Awake; });
        }, "every copy's ragdoll asleep at the rest");
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
    // S1 (the 2026-10-31 playtest, guest-bucket-on-npc-head): a held object went through heads. The hold is
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

SCENARIO("A loaded body takes its rest and a script's move from its body's state alone, not from its move row", "[physics][world-rest-phase]")
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

    // A script's move (the console's setpos): the host's game placed it, its body's state carries it (HostBodies::OnHostPlaced).
    const glm::vec3 cMoved{cRest.x + 50.f, cRest.y, cRest.z};
    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlaceByGame(kBucket, cMoved); });
    party.Until([&] { return glm::distance(watched.Flight.Body.Position, cMoved) < 0.01f && !watched.Awake; }, "the scripted move places the bucket");
    CHECK(watched.MoveRows == 0);
}

SCENARIO("A guest's player moves the bucket: the host's move row of it does not put the guest's back", "[physics][mover]")
{
    // guest-drops-item-stays (2026-11-14): the host's row for the guest's drop put the falling sword back where it was
    // dropped, in the air and asleep, so it never fell in the guest's game.
    Party party(1, Plugins);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    guestBucket.Flight.Floor = kTable;
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {160.f, 0.f, 0.f});
    });
    party.Until([&] { return Moves(party.Guest(), kBucket); }, "the bucket is the guest's");
    NotifyObjectChanges changes;
    const float cPose[6]{100.f, 200.f, kTable + 40.f, 0.f, 0.f, 0.f};
    ObjectChange change{.Type = ObjectChange::Kind::kMove, .Object = kBucket.Object};
    change.Data.assign(reinterpret_cast<const char*>(cPose), sizeof(cPose));
    changes.Changes.push_back(change);
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(changes); });
    for (int i = 0; i < 10; ++i)
        party.Frame();
    THEN("the guest's slides on as its own physics has it")
    {
        CHECK(guestBucket.MoveRows == 0);
        CHECK(guestBucket.Flight.Body.Position.z == kTable);
    }
}

SCENARIO("A host frame from before the guest's push comes after its rest: the guest's stays where it stopped", "[physics][mover]")
{
    // Host frames ride the unreliable channel: one sent before the guest took the body may land after its rest.
    Party party(1, Plugins);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    guestBucket.Flight.Floor = kTable;
    const uint64_t cBefore = party.Guest().GetFrameTick();
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {160.f, 0.f, 0.f});
    });
    party.Until([&] { return Moves(party.Guest(), kBucket); }, "the bucket is the guest's");
    party.Until([&] { return !guestBucket.Awake && !Moves(party.Guest(), kBucket); }, "the guest's bucket rests");
    const glm::vec3 cRest = guestBucket.Flight.Body.Position;
    const auto late = FrameOf(cBefore, kBucket, {100.f, 200.f, kTable}, {50.f, 0.f, 0.f}, 0);
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(late); });
    for (int i = 0; i < 30; ++i)
        party.Frame();
    THEN("the late frame is not played")
    {
        CHECK(guestBucket.Drives == 0);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, cRest) < 0.01f);
    }
}

SCENARIO("The host's game places the bucket a guest's push still moves: it stays where the host's game put it, in every game", "[physics][mover]")
{
    // The duo, 2026-11-15: the host's console moved a cabbage a guest's player had just pushed. The host's game went
    // on following the guest's frames, and the cabbage ended where the guest had it.
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {160.f, 0.f, 0.f});
    });
    party.Until([&] { return hostBucket.Drives > 0; }, "the host's bucket follows the guest's push");
    const glm::vec3 cPlaced{-400.f, 300.f, kTable};
    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlaceByGame(kBucket, cPlaced); });
    const auto cWhere = [&] {
        return std::format("host's at ({:.0f}, {:.0f}), guest's at ({:.0f}, {:.0f}) {}", hostBucket.Flight.Body.Position.x, hostBucket.Flight.Body.Position.y,
                           guestBucket.Flight.Body.Position.x, guestBucket.Flight.Body.Position.y, guestBucket.Awake ? "awake" : "asleep");
    };
    party.Until([&] { return !Moves(party.Guest(), kBucket) && !guestBucket.Awake; }, "the guest gives the bucket back", cWhere);
    for (int i = 0; i < 60; ++i)
        party.Frame();
    THEN("both games have it where the host's game put it")
    {
        INFO(cWhere());
        CHECK(glm::distance(hostBucket.Flight.Body.Position, cPlaced) < 0.01f);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, cPlaced) < 0.01f);
    }
}

SCENARIO("A guest's player bumps the bucket: the host's is never faster than the guest's push made it", "[physics][mover]")
{
    // The duo, 2026-11-15 (carry_into_pile): the host gave its body the guest's impulse and then followed the guest's
    // frames too. Launched twice, a sweet roll flew 133 units off at 535 units/s and was put back in one frame.
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {160.f, 0.f, 0.f});
    });
    float fastest = 0.f, furthest = 0.f;
    party.Until([&] {
        fastest = std::max(fastest, glm::length(hostBucket.Flight.Body.LinearVelocity));
        furthest = std::max(furthest, hostBucket.Flight.Body.Position.x - guestBucket.Flight.Body.Position.x);
        return !guestBucket.Awake && hostBucket.Rests > 0 && !hostBucket.Awake;
    }, "the bucket rests on both games");
    THEN("the host's bucket never outran the guest's")
    {
        CHECK(fastest <= 160.f * 1.5f);
        CHECK(furthest < 16.f);
    }
}

SCENARIO("A guest's copy walks into the bucket in the host's game: its capsule knocks nothing", "[physics][mover]")
{
    // The duo, 2026-11-15 (tavern): the guest's copy walked through the host's pile as its player did the guest's.
    // The host's game moved the items its copy met and followed the guest's for others: two results, 55 to 80 unit
    // jumps. What a player's character walks into is moved by that player's game alone.
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    const glm::vec3 cStart = hostBucket.Flight.Body.Position;
    // The host's game names its copies to the engine each update, once the copy is there.
    party.Until([&] {
        auto& engine = Party::Engine(party.Host());
        return std::ranges::find(engine.CopiesPushNothing, PlayerCopy(engine)) != engine.CopiesPushNothing.end();
    }, "the host's game tells the engine its copy pushes nothing");
    // In the guest's own game its player passes the bucket by; on the host its copy, a step behind, meets it.
    party.As(party.Host(), [&] {
        auto& engine = Party::Engine(party.Host());
        engine.Bump(PlayerCopy(engine), kBucket, {0.f, 300.f, 0.f});
    });
    for (int i = 0; i < 60; ++i)
        party.Frame();
    THEN("the bucket stays where it was in both games")
    {
        CHECK(glm::distance(hostBucket.Flight.Body.Position, cStart) < 0.01f);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, cStart) < 0.01f);
        CHECK(guestBucket.Drives == 0);
    }
}

SCENARIO("A guest's own physics moves a pile's bodies one after another: each gets the host's pose back at once", "[physics][mover]")
{
    // The duo, 2026-11-15 (carry_into_pile): six bodies the guest's physics kept moving were asked one at a time, a
    // second after each moved, and never settled. Now a body the host never sent is hinted at once, and the stream
    // brings its state back.
    Party party(1, Plugins);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    auto& guestPlate = BodyOf(party, party.Guest(), kPlate);
    auto& ownMoves = Party::Engine(party.Guest()).OwnMoves;
    const auto cMove = [&](const BodyKey& acKey, FakeEngine::Body& aBody) {
        ownMoves.push_back({acKey, aBody.Flight.Body.Position, aBody.Flight.Body.Rotation});
        aBody.Flight.Body.Position += glm::vec3(30.f, 0.f, 0.f);
        ownMoves.push_back({acKey, aBody.Flight.Body.Position, aBody.Flight.Body.Rotation});
    };
    cMove(kBucket, guestBucket);
    const auto cLater = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < cLater)
        party.Frame();
    cMove(kPlate, guestPlate);
    party.Until([&] { return guestBucket.Rests > 0 && guestPlate.Rests > 0; }, "the guest gets the host's pose of both");
    CHECK(guestBucket.Rests == 1);
}

SCENARIO("The guest's physics knocks the bucket off the host's rest again and again: it is steered back each time", "[physics][mover]")
{
    // The duo, 2026-11-15 (push_line): a ladle leaning at the host's rest fell in the guest's game, was asked about and
    // put back, and fell again, every 1.2 s for 45 s; then (kAsksAtMost, kAgainAtMost) it was left where it fell,
    // apart. Now it is kept at the host's rest by steering (RestPlacer::kKeep).
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    guestBucket.Flight.Floor = kTable;
    float furthest = 0.f;
    for (int fall = 0; fall < 5; ++fall)
    {
        // Its own physics moves it alone (the host's never does): left alone it slides 18 units off.
        party.As(party.Guest(), [&] { Party::Engine(party.Guest()).Knock(kBucket, {120.f, 0.f, 0.f}); });
        const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
        while (std::chrono::steady_clock::now() < cUntil)
        {
            party.Frame();
            furthest = std::max(furthest, glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position));
        }
        party.Until([&] { return glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < BodyTolerances::kSameDistance; },
                    "the guest's bucket is back at the host's rest");
    }
    INFO("the guest's bucket got " << furthest << " units off at most");
    CHECK(furthest > 0.5f);
    CHECK(furthest < 10.f);
}

SCENARIO("The host's rest of the bucket has its contacts in the guest's game: the guest's push slides it, not throws it", "[physics][mover]")
{
    // The duo, 2026-11-15 (push_mixed): the host's rest was set dynamic and put to sleep at once on the guest, so Havok
    // had no floor contact for it; the guest's player's push drove it into the floor and it bounced 30 units up (10
    // of 13 pushes; vanilla and the host's own never).
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    party.Frame();
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Knock(kBucket, {120.f, 0.f, 0.f}); });
    party.Until([&] { return !hostBucket.Awake && guestBucket.Rests > 0; }, "the host's bucket rests on both games");
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.Bump(0x14, kBucket, {160.f, 0.f, 0.f});
    });
    THEN("it leaves along the floor")
    {
        CHECK(guestBucket.Thrown == 0);
        CHECK(guestBucket.Flight.Body.LinearVelocity.z == 0.f);
    }
}

SCENARIO("The host puts the guest's rest of the bucket in place: its step awake there is not the host moving it", "[physics][mover]")
{
    // The duo, 2026-11-15 (push_mixed): the host's game read the bucket awake in the step its guest's rest is put
    // there awake (for its contacts), took it for its own move and began the rest again: it hopped there for 15 s
    // and the host streamed it to the guest (350 frames), whose bucket crept after it.
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {160.f, 0.f, 0.f});
    });
    // Frames go past 2 units (BodySendLimits): the host's played bucket stops short of where the guest's rests.
    party.Until([&] {
        if (!hostBucket.RestAttempts)
            hostBucket.Flight.Body.Position = guestBucket.Flight.Body.Position - glm::vec3(1.5f, 0.f, 0.f);
        return hostBucket.Rests > 0;
    }, "the host puts the guest's rest");
    const size_t cDrives = guestBucket.Drives;
    for (int i = 0; i < 60; ++i)
        party.Frame();
    THEN("it lies there asleep in the host's game, which sends the guest no frames of it")
    {
        CHECK_FALSE(hostBucket.Awake);
        CHECK(guestBucket.Drives == cDrives);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < 0.01f);
    }
}

SCENARIO("A guest's player bumps the bucket into the plate: the plate is the guest's too, and lies where its game put it", "[physics][mover]")
{
    // The duo, 2026-11-15 (carry_into_pile): only what the guest's player touched was the guest's. The pile its held
    // bucket knocked was simulated by both games, the host's result steered the guest's pile against the bucket, and
    // the jam dropped the hold after 1.4 s.
    Party party(1, Plugins);
    auto& hostPlate = BodyOf(party, party.Host(), kPlate);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    auto& guestPlate = BodyOf(party, party.Guest(), kPlate);
    for (auto* pBody : {&hostPlate, &guestBucket, &guestPlate, &BodyOf(party, party.Host(), kBucket)})
        pBody->Flight.Floor = kTable;
    // Beside the bucket, as in a pile.
    guestPlate.Flight.Body.Position = hostPlate.Flight.Body.Position = guestBucket.Flight.Body.Position + glm::vec3(40.f, 0.f, 0.f);
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {160.f, 0.f, 0.f});
        // The bucket runs into the plate in the guest's game (Havok pairs them); no player touched the plate.
        engine.Lands(kBucket, kPlate);
        engine.Knock(kPlate, {120.f, 60.f, 0.f});
    });
    party.Until([&] { return !guestPlate.Awake && hostPlate.Rests > 0 && !hostPlate.Awake; }, "the plate rests on both games");
    for (int i = 0; i < 30; ++i)
        party.Frame();
    THEN("the guest's plate was never placed by another game, and the host's lies where the guest's stopped")
    {
        CHECK(guestPlate.Drives == 0);
        CHECK(guestPlate.Rests == 0);
        CHECK(glm::distance(guestPlate.Flight.Body.Position, hostPlate.Flight.Body.Position) < 0.01f);
    }
}

SCENARIO("A guest's bucket, played on the host, runs into a plate the guest's never hit: the plate stays", "[physics][mover]")
{
    // The duo, 2026-11-15 (tavern): steered onto the guest's frames, the host's copy of a guest's object knocked a
    // neighbour the guest's game never hit. The host simulated that neighbour itself, and the guest's, at rest, jumped
    // 26 units after it. A played object meets the world, not objects nobody moves (Physics/BodyGroups.h).
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& hostPlate = BodyOf(party, party.Host(), kPlate);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    auto& guestPlate = BodyOf(party, party.Guest(), kPlate);
    for (auto* pBody : {&hostBucket, &hostPlate, &guestBucket, &guestPlate})
        pBody->Flight.Floor = kTable;
    // The plate well off the bucket's way in the guest's game (out of its reach).
    guestPlate.Flight.Body.Position = hostPlate.Flight.Body.Position = guestBucket.Flight.Body.Position + glm::vec3(0.f, 300.f, 0.f);
    const glm::vec3 cPlate = hostPlate.Flight.Body.Position;
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {160.f, 0.f, 0.f});
    });
    party.Until([&] { return hostBucket.Drives > 0; }, "the host's bucket follows the guest's push");
    // On the host the steered bucket, a little off the guest's path, touches the plate.
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Collide(kBucket, kPlate, {0.f, 200.f, 0.f}); });
    party.Until([&] { return !guestBucket.Awake && hostBucket.Rests > 0 && !hostBucket.Awake; }, "the bucket rests on both games");
    for (int i = 0; i < 30; ++i)
        party.Frame();
    THEN("the plate never moved in either game")
    {
        CHECK(glm::distance(hostPlate.Flight.Body.Position, cPlate) < 0.01f);
        CHECK(glm::distance(guestPlate.Flight.Body.Position, cPlate) < 0.01f);
        CHECK(guestPlate.Drives == 0);
    }
    AND_THEN("at its rest the host's bucket is nobody's again: it meets what it lies beside")
    {
        CHECK(!Party::Engine(party.Host()).Classes.contains(kBucket));
        CHECK(!Party::Engine(party.Guest()).Classes.contains(kBucket));
    }
}

SCENARIO("A guest's player knocks the bucket and it rests: the host's move row of that rest does not put it back", "[physics][mover]")
{
    // The duo, 2026-11-16 (the playtest's cheese wheels, pile_carry_release): the host put the guest's rest a step
    // awake, it settled a unit, and its move row came back. Applied, it keyframed the guest's wheel there and slept its
    // island (60850), the wheel the guest had just let go falling beside it too: it hung in the air.
    Party party(1, Plugins);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    guestBucket.Flight.Floor = kTable;
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {160.f, 0.f, 0.f});
    });
    party.Until([&] { return Moves(party.Guest(), kBucket); }, "the bucket is the guest's");
    party.Until([&] { return !Moves(party.Guest(), kBucket); }, "the guest's bucket rests and its rest goes out");
    const glm::vec3 cRest = guestBucket.Flight.Body.Position;
    NotifyObjectChanges changes;
    const float cPose[6]{cRest.x + 1.f, cRest.y, cRest.z, 0.f, 0.f, 0.f};
    ObjectChange change{.Type = ObjectChange::Kind::kMove, .Object = kBucket.Object};
    change.Data.assign(reinterpret_cast<const char*>(cPose), sizeof(cPose));
    changes.Changes.push_back(change);
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(changes); });
    for (int i = 0; i < 10; ++i)
        party.Frame();
    THEN("the guest's lies where its own physics put it, untouched")
    {
        CHECK(guestBucket.MoveRows == 0);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, cRest) < 0.01f);
    }
}

SCENARIO("A guest's player drops the bucket onto the plate: the plate it presses aside is the guest's, and the host's rest of it is not put back", "[physics][mover]")
{
    // The duo, 2026-11-22 (drop_wheel_on_wheels: rests 3 to 6 units apart); physics-sync.md, "A drop onto a pile".
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& hostPlate = BodyOf(party, party.Host(), kPlate);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    auto& guestPlate = BodyOf(party, party.Guest(), kPlate);
    for (auto* pBody : {&hostBucket, &hostPlate, &guestBucket, &guestPlate})
        pBody->Flight.Floor = kTable;
    const glm::vec3 cPlate = guestPlate.Flight.Body.Position;
    // Held over the plate, then let go: it falls onto it.
    Party::Engine(party.Guest()).Grabbed = kBucket;
    guestBucket.Flight.Body.Position = cPlate + glm::vec3(0.f, 0.f, 40.f);
    party.Until([&] { return hostBucket.Drives > 0; }, "the host's bucket follows the guest's hands");
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.Grabbed.reset();
        engine.Knock(kBucket, {30.f, 0.f, -60.f});
        // Their islands merge as the AABBs meet, before they touch.
        engine.Lands(kBucket, kPlate);
    });
    party.Frame();
    // Pressed aside under it: no push of its own, less than a knock's 2 units.
    guestPlate.Flight.Body.Position += glm::vec3(1.5f, 0.f, 0.f);
    // The host's pile woke as the bucket was put over it and lay still: its rest of the plate goes to the guest.
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Wake(kPlate, 48); });
    party.Until([&] { return !guestBucket.Awake && hostBucket.Rests > 0 && !hostBucket.Awake && !hostPlate.Awake; }, "the bucket rests on both games");
    for (int i = 0; i < 90; ++i)
        party.Frame();
    THEN("the guest's plate was never put back by the host's rest, and the host's lies where the guest's was pressed to")
    {
        CHECK(guestPlate.Rests == 0);
        CHECK(glm::distance(guestPlate.Flight.Body.Position, cPlate + glm::vec3(1.5f, 0.f, 0.f)) < 0.01f);
        CHECK(glm::distance(hostPlate.Flight.Body.Position, guestPlate.Flight.Body.Position) < 0.01f);
    }
}

SCENARIO("A guest's player leans on the bucket and shifts it a unit: the host's lies there too, and its old rest is not put back", "[physics][mover]")
{
    // The duo, 2026-11-22 (drop_wheel_on_wheels: put back into the player's capsule, 62648 pushed it out for 15 s).
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    const glm::vec3 cLay = guestBucket.Flight.Body.Position;
    party.As(party.Guest(), [&] {
        Party::Engine(party.Guest()).PlayerPushes.push_back({kBucket, {4.f, 0.f, 0.f}, cLay});
    });
    party.Frame();
    // Shifted by the capsule, no flight of its own.
    guestBucket.Flight.Body.Position = cLay + glm::vec3(1.6f, 0.f, 0.f);
    // The host's bucket wakes where it lies and sleeps again: its rest goes to the guest.
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Wake(kBucket, 48); });
    party.Until([&] { return glm::distance(hostBucket.Flight.Body.Position, guestBucket.Flight.Body.Position) < 0.01f && !hostBucket.Awake; },
                "the host's bucket lies where the guest's player shifted it");
    for (int i = 0; i < 90; ++i)
        party.Frame();
    THEN("the guest's was never put back, and both lie where it was shifted to")
    {
        CHECK(guestBucket.Rests == 0);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, cLay + glm::vec3(1.6f, 0.f, 0.f)) < 0.01f);
        CHECK(glm::distance(hostBucket.Flight.Body.Position, guestBucket.Flight.Body.Position) < 0.01f);
    }
}

SCENARIO("A guest's player knocks the bucket and the plate: the plate's rest waits for the bucket's, and both go to the host together", "[physics][mover]")
{
    // The duo, 2026-11-22 (carry_into_pile: one body claimed and rested 76 times in a trial).
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& hostPlate = BodyOf(party, party.Host(), kPlate);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    auto& guestPlate = BodyOf(party, party.Guest(), kPlate);
    for (auto* pBody : {&hostBucket, &hostPlate, &guestBucket, &guestPlate})
        pBody->Flight.Floor = kTable;
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {400.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.PlayerPushes.push_back({kPlate, {20.f, 0.f, 0.f}, guestPlate.Flight.Body.Position});
        // One pile (one island); the bucket slides far, the plate a little.
        engine.Lands(kBucket, kPlate);
        engine.Knock(kBucket, {-400.f, 0.f, 0.f});
        engine.Knock(kPlate, {20.f, 30.f, 0.f});
    });
    party.Until([&] { return !guestPlate.Awake && glm::length(guestPlate.Flight.Body.LinearVelocity) == 0.f; }, "the guest's plate lies still");
    REQUIRE(guestBucket.Awake);
    for (int i = 0; i < 10; ++i)
        party.Frame();
    THEN("the plate is still the guest's while its bucket slides: its rest has not gone out")
    {
        REQUIRE(guestBucket.Awake);
        CHECK(Moves(party.Guest(), kPlate));
        CHECK(hostPlate.Rests == 0);
    }
    party.Until([&] { return !guestBucket.Awake && hostBucket.Rests > 0 && hostPlate.Rests > 0 && !hostBucket.Awake && !hostPlate.Awake; },
                "both rest on both games");
    AND_THEN("both lie where the guest's game put them")
    {
        CHECK(glm::distance(guestPlate.Flight.Body.Position, hostPlate.Flight.Body.Position) < 0.01f);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < 0.01f);
    }
}

SCENARIO("A guest's bucket lands on the plate and shifts it before the guest's game looks: the shift is still the guest's", "[physics][mover]")
{
    // Review of PR #101: a shift between two updates was in the first read of the pile, so it was never seen.
    Party party(1, Plugins);
    auto& hostPlate = BodyOf(party, party.Host(), kPlate);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    auto& guestPlate = BodyOf(party, party.Guest(), kPlate);
    for (auto* pBody : {&hostPlate, &guestBucket, &guestPlate, &BodyOf(party, party.Host(), kBucket)})
        pBody->Flight.Floor = kTable;
    const glm::vec3 cPlate = guestPlate.Flight.Body.Position;
    // The guest knows the host's rest of the plate.
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Wake(kPlate, 48); });
    party.Until([&] { return guestPlate.Rests > 0; }, "the host's rest of the plate reaches the guest");
    const size_t cRests = guestPlate.Rests;
    Party::Engine(party.Guest()).Grabbed = kBucket;
    guestBucket.Flight.Body.Position = cPlate + glm::vec3(0.f, 0.f, 40.f);
    party.Until([&] { return BodyOf(party, party.Host(), kBucket).Drives > 0; }, "the host's bucket follows the guest's hands");
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.Grabbed.reset();
        engine.Knock(kBucket, {30.f, 0.f, -60.f});
        engine.Lands(kBucket, kPlate);
        // In the same step: the guest's game never read the plate where it lay.
        guestPlate.Flight.Body.Position += glm::vec3(1.5f, 0.f, 0.f);
    });
    party.Until([&] { return !guestBucket.Awake && glm::distance(hostPlate.Flight.Body.Position, guestPlate.Flight.Body.Position) < 0.01f && !hostPlate.Awake; },
                "the host's plate lies where the guest's was shifted to");
    THEN("the guest's plate was not put back")
    {
        CHECK(guestPlate.Rests == cRests);
        CHECK(glm::distance(guestPlate.Flight.Body.Position, cPlate + glm::vec3(1.5f, 0.f, 0.f)) < 0.01f);
    }
}

SCENARIO("The host's player pushes the plate the guest's bucket lands on: the last toucher has it, its rest is put on the guest's", "[physics][mover]")
{
    // Review of PR #101: a host rest of a body in the guest's pile that the guest never claimed was dropped for good.
    Party party(1, Plugins);
    auto& hostPlate = BodyOf(party, party.Host(), kPlate);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    auto& guestPlate = BodyOf(party, party.Guest(), kPlate);
    for (auto* pBody : {&hostPlate, &guestBucket, &guestPlate, &BodyOf(party, party.Host(), kBucket)})
        pBody->Flight.Floor = kTable;
    const glm::vec3 cPlate = guestPlate.Flight.Body.Position;
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {-300.f, 0.f, 0.f});
        engine.Lands(kBucket, kPlate);
    });
    party.Frame();
    // The host's player pushes its plate while the guest's pile is awake: the last toucher takes it.
    party.As(party.Host(), [&] {
        auto& engine = Party::Engine(party.Host());
        engine.PlayerPushes.push_back({kPlate, {0.f, 120.f, 0.f}, hostPlate.Flight.Body.Position});
        engine.Knock(kPlate, {0.f, 120.f, 0.f});
    });
    // Whoever touched it last (the guest's bucket may knock it again after the push), both games end with one plate.
    party.Until([&] { return !hostPlate.Awake && !guestBucket.Awake && !guestPlate.Awake &&
                             glm::distance(hostPlate.Flight.Body.Position, guestPlate.Flight.Body.Position) < BodyTolerances::kSameDistance; },
                "the plate lies in one place in both games once the pile is still");
    for (int i = 0; i < 60; ++i)
        party.Frame();
    THEN("the guest's plate lies where the host's does, moved off where it lay")
    {
        CHECK(glm::distance(hostPlate.Flight.Body.Position, guestPlate.Flight.Body.Position) < BodyTolerances::kSameDistance);
        CHECK(glm::distance(hostPlate.Flight.Body.Position, cPlate) > 1.f);
    }
}

SCENARIO("The host's game places a bucket whose rest it took from a guest: it stays where the host's game put it", "[physics][mover]")
{
    // The duo, 2026-11-23 (push_line's staging): the host's game had put a guest's rest of the ladle and kept that rest's
    // placer; the console's setpos later woke it off that rest, and the placer took it back there.
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {160.f, 0.f, 0.f});
    });
    party.Until([&] { return !Moves(party.Guest(), kBucket) && !guestBucket.Awake && hostBucket.Rests > 0 && !hostBucket.Awake; },
                "the guest's rest is put on the host's bucket");
    // The console moves it, then wakes it (the harness's wake): it falls onto the table there.
    const glm::vec3 cPlaced{-400.f, 300.f, kTable + 16.f};
    party.As(party.Host(), [&] {
        auto& engine = Party::Engine(party.Host());
        engine.PlaceByGame(kBucket, cPlaced);
        engine.Knock(kBucket, {});
    });
    party.Until([&] { return !hostBucket.Awake && hostBucket.Flight.Body.Position.z <= kTable + 0.01f; }, "the host's bucket lands");
    party.Until([&] { return !guestBucket.Awake && glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < BodyTolerances::kSameDistance; },
                "the guest's bucket lies at the host's rest");
    for (int i = 0; i < 60; ++i)
        party.Frame();
    THEN("both games have it where the host's game put it, not back at the guest's old rest")
    {
        INFO(std::format("host's at ({:.2f}, {:.2f}, {:.2f}) {}, guest's at ({:.2f}, {:.2f}, {:.2f}) {}", hostBucket.Flight.Body.Position.x,
                         hostBucket.Flight.Body.Position.y, hostBucket.Flight.Body.Position.z, hostBucket.Awake ? "awake" : "asleep",
                         guestBucket.Flight.Body.Position.x, guestBucket.Flight.Body.Position.y, guestBucket.Flight.Body.Position.z,
                         guestBucket.Awake ? "awake" : "asleep"));
        CHECK(glm::distance(glm::vec2(hostBucket.Flight.Body.Position), glm::vec2(cPlaced)) < 0.01f);
        CHECK(glm::distance(guestBucket.Flight.Body.Position, hostBucket.Flight.Body.Position) < BodyTolerances::kSameDistance);
    }
}

SCENARIO("A guest's player leans on the bucket it shifted for four seconds: it stays the guest's, not taken as silent", "[physics][mover]")
{
    Party party(1, Plugins);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    const auto cLean = [&] {
        party.As(party.Guest(), [&] { Party::Engine(party.Guest()).PlayerPushes.push_back({kBucket, {1.f, 0.f, 0.f}, guestBucket.Flight.Body.Position}); });
    };
    cLean();
    party.Until([&] { return Moves(party.Guest(), kBucket); }, "the bucket is the guest's");
    const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    bool kept = true;
    while (std::chrono::steady_clock::now() < cUntil)
    {
        cLean();
        party.Frame();
        kept &= Moves(party.Guest(), kBucket);
    }
    CHECK(kept);
}

SCENARIO("The host's game hangs a bucket in the air (a rack's weapon, the console): the guest's hangs there too, asleep", "[physics][placed]")
{
    // The duo, 2026-11-23 (object-rests-beside-orgnar): the Sleeping Giant's rack weapons, placed by the host's game,
    // were put awake for a step on the guest and fell to the floor. A body the game leaves asleep in a pose it cannot
    // hold awake is held there (the old move row's keyframe and free asleep: HoldAt).
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    const glm::vec3 cHung{150.f, 260.f, kTable + 60.f};
    hostBucket.OnlyAsleep = guestBucket.OnlyAsleep = true; // a rack holds it only asleep
    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlaceByGame(kBucket, cHung); });
    party.Until([&] { return glm::distance(guestBucket.Flight.Body.Position, cHung) < 0.01f && guestBucket.Rests > 0; }, "the guest's bucket is put there");
    for (int i = 0; i < 60; ++i)
        party.Frame();
    THEN("it hangs there asleep in both games")
    {
        CHECK(glm::distance(guestBucket.Flight.Body.Position, cHung) < 0.01f);
        CHECK_FALSE(guestBucket.Awake);
        CHECK(glm::distance(hostBucket.Flight.Body.Position, cHung) < 0.01f);
    }
}

SCENARIO("The host's console moves the bucket an axis at a time through the plate: the guest's goes straight to the last", "[physics][placed]")
{
    // The console's setpos goes x, y, z, one a frame, through whatever lies between. Each is a placement of the host's
    // game; the playback (100 ms behind) keeps only the newest rest, so the guest's body never stops inside the plate.
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    const glm::vec3 cPlate = BodyOf(party, party.Host(), kPlate).Flight.Body.Position;
    const glm::vec3 cThrough{cPlate.x, hostBucket.Flight.Body.Position.y, kTable}; // inside the plate
    const glm::vec3 cLast{cPlate.x, cPlate.y + 60.f, kTable};
    // One move a frame, as the console's setpos runs.
    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlaceByGame(kBucket, cThrough); });
    party.Frame();
    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlaceByGame(kBucket, cLast); });
    bool simulatedInside = false;
    party.Until([&] {
        simulatedInside |= !guestBucket.Held && glm::distance(guestBucket.Flight.Body.Position, cThrough) < 0.01f;
        return glm::distance(guestBucket.Flight.Body.Position, cLast) < 0.01f && !guestBucket.Held;
    }, "the guest's bucket lies at the console's last move, freed");
    CHECK_FALSE(simulatedInside);
    CHECK_FALSE(guestBucket.Awake);
}

SCENARIO("The host's console moves the bucket as the guest's rest of it is being put in its game: it lies where the console put it", "[physics][placed]")
{
    // The duo, 2026-11-23 (guest-push-like-vanilla): the host's game still played the guest's ladle to its rest, and
    // steered it back there from where setpos had put it.
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    hostBucket.Flight.Floor = guestBucket.Flight.Floor = kTable;
    party.As(party.Guest(), [&] {
        auto& engine = Party::Engine(party.Guest());
        engine.PlayerPushes.push_back({kBucket, {160.f, 0.f, 0.f}, guestBucket.Flight.Body.Position});
        engine.Knock(kBucket, {160.f, 0.f, 0.f});
    });
    party.Until([&] { return Moves(party.Guest(), kBucket); }, "the bucket is the guest's");
    party.Until([&] { return !Moves(party.Guest(), kBucket); }, "the guest's bucket rests and its rest goes out");
    const int cAfter = GENERATE(0, 1, 3, 8, 15);
    for (int i = 0; i < cAfter; ++i)
        party.Frame();
    const glm::vec3 cLast{150.f, 460.f, kTable};
    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlaceByGame(kBucket, cLast); });
    // Past the guest's playback delay and the rest's steps, in the network's time.
    const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (std::chrono::steady_clock::now() < cUntil)
        party.Frame();
    INFO("placed " << cAfter << " frames after the rest; the host's at y " << hostBucket.Flight.Body.Position.y << ", the guest's at y " << guestBucket.Flight.Body.Position.y);
    CHECK(glm::distance(hostBucket.Flight.Body.Position, cLast) < 0.5f);
    CHECK(glm::distance(guestBucket.Flight.Body.Position, cLast) < 0.5f);
}

SCENARIO("The host's bucket lies on what holds it only there: the guest's, woken again and again, is kept at the host's rest, never snapped", "[physics][kept-rest]")
{
    // The playtest of 2026-11-23 and mask_on_orgnar_lies_as_hosts: a mask on Orgnar's back. It is woken
    // through his animated body (measured on the guest: 13486, 78910). What holds it only in the host's game is not
    // known; here a floor does. The guest's fell, was glided back and put (a 3.7 unit jump), twice, then left 3 units off.
    Party party(1, Plugins);
    auto& hostBucket = BodyOf(party, party.Host(), kBucket);
    auto& guestBucket = BodyOf(party, party.Guest(), kBucket);
    // Held up in the host's game; nothing under it in the guest's but the table, 40 units down.
    hostBucket.Flight.Floor = kTable + 40.f;
    guestBucket.Flight.Floor = kTable;
    // It falls onto that in the host's game: a rest of its physics, not a placement.
    party.As(party.Host(), [&] {
        hostBucket.Flight.Body.Position.z = kTable + 60.f;
        Party::Engine(party.Host()).Knock(kBucket, {});
    });
    party.Until([&] { return !hostBucket.Awake && guestBucket.Rests > 0 && !guestBucket.Awake; }, "the bucket rests on both games");
    const glm::vec3 cOn = hostBucket.Flight.Body.Position;
    REQUIRE(glm::distance(guestBucket.Flight.Body.Position, cOn) < 0.5f);
    float furthestStep = 0.f, furthestOff = 0.f;
    glm::vec3 last = guestBucket.Flight.Body.Position;
    for (int wake = 0; wake < 6; ++wake)
    {
        // Woken, not pushed: awake with nothing under it.
        party.As(party.Guest(), [&] { Party::Engine(party.Guest()).Knock(kBucket, {}); });
        // The fake's physics steps in the network's time: a second of it for each wake.
        const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        while (std::chrono::steady_clock::now() < cUntil)
        {
            party.Frame();
            furthestStep = std::max(furthestStep, glm::distance(guestBucket.Flight.Body.Position, last));
            furthestOff = std::max(furthestOff, glm::distance(guestBucket.Flight.Body.Position, cOn));
            last = guestBucket.Flight.Body.Position;
        }
    }
    INFO("the guest's bucket stepped " << furthestStep << " units in a frame at most, and was " << furthestOff << " off the host's rest at worst");
    CHECK(furthestStep < 2.f);
    CHECK(furthestOff < 5.f);
    CHECK(glm::distance(guestBucket.Flight.Body.Position, cOn) < 2.f);
}
