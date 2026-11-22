// HostHands: a guest's own bodies, its player's hand on the host's and its ragdoll (docs/design/physics-sync.md, "Owners").
#include "session.h"

namespace
{
const BodyKey kLadle{.Object = GameId(0, 0x0001A0F3)};

BodyFramesRequest Held(uint64_t aTick, float aX, bool aHeld = true)
{
    BodyFramesRequest held;
    held.Tick = aTick;
    held.Held = aHeld;
    held.Frames = {{.Key = kLadle, .Position = {aX, 0.f, 100.f}, .LinearVelocity = {50.f, 0.f, 0.f}}};
    return held;
}

// A guest's game moving the ladle it pushed or let go, not in its hands.
BodyFramesRequest Moved(uint64_t aTick, float aX)
{
    return Held(aTick, aX, false);
}

BodyRestsRequest RestsAt(uint64_t aTick, float aX)
{
    BodyRestsRequest rests;
    rests.Tick = aTick;
    rests.Rests = {{.Key = kLadle, .Position = {aX, 0.f, 0.f}}};
    return rests;
}
} // namespace

TEST_CASE("Physics: a guest's player holds a body, its alone: every other game plays the holder's frames", "[HostAuthority][Physics]")
{
    // Exclusive ownership (2026-11-02): host-cannot-take-guest-hold, the host's player grabbed the guest's bucket.
    Session session;
    session.Receive(kPeer, Held(1000, 10.f));
    session.Receive(kPeer, Held(1050, 12.5f));
    THEN("the host's game and the other guests get the holder's frames as a hold; the holder gets none back")
    {
        for (const auto cGame : {kHost, kOtherPeer})
        {
            REQUIRE(session.SentTo<NotifyBodyFrames>(cGame).size() == 2);
            CHECK(session.SentTo<NotifyBodyFrames>(cGame).back()->Held);
            CHECK(session.SentTo<NotifyBodyFrames>(cGame).back()->Frames[0].Position.x == 12.5f);
        }
        CHECK(session.SentTo<NotifyBodyFrames>(kPeer).empty());
    }
    THEN("a second guest's hold of it is not taken")
    {
        session.Outbox.clear();
        session.Receive(kOtherPeer, Held(1100, 40.f));
        CHECK(session.SentTo<NotifyBodyFrames>(kHost).empty());
        CHECK(session.SentTo<NotifyBodyFrames>(kPeer).empty());
    }
}

TEST_CASE("Physics: the host's player holds a body: each guest gets it as a hold, the rest as plain frames", "[HostAuthority][Physics]")
{
    // A guest plays plain frames simulated (its grab takes them); a hold keyframed, so its grab refuses it.
    Session session;
    const BodyKey cBucket{.Object = GameId(0, 0x0001A0F4)};
    session.Moving.Awake = {{.Key = kLadle, .Position = {100.f, 0.f, 0.f}, .LinearVelocity = {300.f, 0.f, 0.f}},
                            {.Key = cBucket, .Position = {120.f, 0.f, 0.f}, .LinearVelocity = {300.f, 0.f, 0.f}}};
    session.Moving.Held = {kLadle};
    session.Frame(1000);
    for (const auto cGuest : {kPeer, kOtherPeer})
    {
        std::vector<std::pair<BodyKey, bool>> sent;
        for (const auto* pFrames : session.SentTo<NotifyBodyFrames>(cGuest))
            for (const auto& cFrame : pFrames->Frames)
                sent.emplace_back(cFrame.Key, pFrames->Held);
        std::ranges::sort(sent);
        CHECK(sent == std::vector<std::pair<BodyKey, bool>>{{kLadle, true}, {cBucket, false}});
    }
}

TEST_CASE("Physics: a guest's player lets go, the host simulates it from there, still the guest's until its rest", "[HostAuthority][Physics]")
{
    Session session;
    session.Receive(kPeer, Held(1000, 10.f));
    BodyReleaseRequest released;
    released.Tick = 1100;
    released.Frames = {{.Key = kLadle, .Position = {15.f, 0.f, 100.f}, .LinearVelocity = {200.f, 0.f, 50.f}}};
    session.Receive(kPeer, released);
    REQUIRE(session.Freed.size() == 1);
    CHECK(session.Freed[0] == released.Frames[0].Quantized());

    THEN("a frame of the hold that comes after the release does not take it again")
    {
        session.Outbox.clear();
        session.Receive(kPeer, Held(1090, 14.f));
        CHECK(session.SentTo<NotifyBodyFrames>(kHost).empty());
    }
    THEN("every other game's hold of it ends with the let go")
    {
        for (const auto cGame : {kHost, kOtherPeer})
        {
            REQUIRE_FALSE(session.SentTo<NotifyBodyFrames>(cGame).empty());
            CHECK_FALSE(session.SentTo<NotifyBodyFrames>(cGame).back()->Held);
        }
    }
    THEN("nobody else takes it while it falls; anyone may once it rests in the guest's game")
    {
        session.Outbox.clear();
        session.Receive(kOtherPeer, Held(1200, 30.f));
        CHECK(session.SentTo<NotifyBodyFrames>(kHost).empty());
        session.Receive(kPeer, RestsAt(1300, 20.f));
        session.Receive(kOtherPeer, Held(1400, 30.f));
        CHECK(session.SentTo<NotifyBodyFrames>(kHost).size() == 1);
    }
}

TEST_CASE("Physics: a body a guest's player moved is that game's until it rests there", "[HostAuthority][Physics]")
{
    // physics-sync.md, "The mover's until its rest": the host's own fall of a guest's let go ladle rested 10 units
    // from the guest's, and that rest snapped the guest's.
    Session session;
    session.Receive(kPeer, Moved(1000, 10.f));
    THEN("its frames go to every other game, the host's too, as plain frames (played steered)")
    {
        for (const auto cGame : {kHost, kOtherPeer})
        {
            REQUIRE(session.SentTo<NotifyBodyFrames>(cGame).size() == 1);
            CHECK_FALSE(session.SentTo<NotifyBodyFrames>(cGame).back()->Held);
        }
        CHECK(session.SentTo<NotifyBodyFrames>(kPeer).empty());
    }
    THEN("the host's own frames and rest of it are not sent meanwhile; the guest's rest goes to every other game")
    {
        session.Outbox.clear();
        session.Moving.Awake = {{.Key = kLadle, .Position = {12.f, 0.f, 0.f}, .LinearVelocity = {300.f, 0.f, 0.f}}};
        session.Frame(1100);
        session.Moving.Rested = {{.Key = kLadle, .Position = {14.f, 0.f, 0.f}}};
        session.Frame(1200);
        CHECK(session.SentTo<NotifyBodyFrames>(kPeer).empty());
        CHECK(session.SentTo<NotifyBodyRests>(kPeer).empty());
        session.Receive(kPeer, RestsAt(1300, 20.f));
        for (const auto cGame : {kHost, kOtherPeer})
        {
            REQUIRE(session.SentTo<NotifyBodyRests>(cGame).size() == 1);
            CHECK(session.SentTo<NotifyBodyRests>(cGame).back()->Rests[0].Position.x == 20.f);
        }
        CHECK(session.SentTo<NotifyBodyRests>(kPeer).empty());
        AND_THEN("the host's game at rest there sends nothing more; moved on from there, it is the host's again")
        {
            session.Moving.Rested = {{.Key = kLadle, .Position = {20.f, 0.f, 0.f}}};
            session.Frame(1400);
            CHECK(session.SentTo<NotifyBodyRests>(kPeer).empty());
            session.Moving.Rested = {{.Key = kLadle, .Position = {60.f, 0.f, 0.f}}};
            session.Frame(1500);
            CHECK(session.SentTo<NotifyBodyRests>(kPeer).size() == 1);
        }
    }
    THEN("another guest's rest of it is not taken, nor a push")
    {
        session.Outbox.clear();
        session.Receive(kOtherPeer, RestsAt(1300, 90.f));
        CHECK(session.SentTo<NotifyBodyRests>(kHost).empty());
    }
    THEN("its game gone silent, it is the host's again where it is, not dropped")
    {
        session.Frame(1000);
        session.Frame(1001 + HostHands::kSilentMs);
        CHECK(session.Freed.empty());
        session.Outbox.clear();
        session.Moving.Rested = {{.Key = kLadle, .Position = {30.f, 0.f, 0.f}}};
        session.Frame(1100 + HostHands::kSilentMs);
        CHECK(session.SentTo<NotifyBodyRests>(kPeer).size() == 1);
    }
}

TEST_CASE("Physics: a holder whose frames stop drops what it held where it was", "[HostAuthority][Physics]")
{
    Session session;
    session.Frame(1000);
    session.Receive(kPeer, Held(1000, 10.f));
    session.Frame(1000 + HostHands::kSilentMs);
    CHECK(session.Freed.empty());
    session.Frame(1001 + HostHands::kSilentMs);
    REQUIRE(session.Freed.size() == 1);
    CHECK(session.Freed[0].Position.x == 10.f);
    CHECK(session.Freed[0].LinearVelocity == glm::vec3(0.f));
}

TEST_CASE("Physics: a holder leaving drops what it held where it was", "[HostAuthority][Physics]")
{
    Session session;
    session.Receive(kPeer, Held(1000, 10.f));
    session.Leave(kPeer);
    REQUIRE(session.Freed.size() == 1);
    CHECK(session.Freed[0].Position.x == 10.f);
    CHECK(session.Freed[0].LinearVelocity == glm::vec3(0.f));
}

TEST_CASE("Physics: a guest whose own physics moved the host's body gets the host's pose, the others nothing", "[HostAuthority][Physics]")
{
    Session session;
    session.Bodies = {{.Key = kLadle, .Position = {10.f, 20.f, 30.f}}};
    BodyPoseRequest asked;
    asked.Rests = {{.Key = kLadle, .Position = {400.f, 20.f, 0.f}}, {.Key = {.Object = GameId(0, 0x1234)}}};
    session.Receive(kPeer, asked);
    REQUIRE(session.SentTo<NotifyBodyRests>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyBodyRests>(kPeer)[0]->Rests == session.Bodies);
    CHECK(session.SentTo<NotifyBodyRests>(kOtherPeer).empty());
    CHECK(session.SentTo<NotifyBodyRests>(kHost).empty());
}

TEST_CASE("Physics: a guest gets the frames of the bodies near its character, every guest the rests", "[HostAuthority][Physics]")
{
    Session session;
    const BodyKey cBucket{.Object = GameId(0, 0x0001A0F4)};
    session.PlayerPositions[session.PlayerId(kPeer)] = {0.f, 0.f, 0.f};
    session.PlayerPositions[session.PlayerId(kOtherPeer)] = {20000.f, 0.f, 0.f};
    session.Moving.Awake = {{.Key = kLadle, .Position = {100.f, 0.f, 0.f}, .LinearVelocity = {300.f, 0.f, 0.f}},
                            {.Key = cBucket, .Position = {19900.f, 0.f, 0.f}, .LinearVelocity = {300.f, 0.f, 0.f}}};
    session.Frame(1000);
    const auto cKeysTo = [&](ConnectionId_t aGuest) {
        std::vector<BodyKey> keys;
        for (const auto* pFrames : session.SentTo<NotifyBodyFrames>(aGuest))
            for (const auto& cFrame : pFrames->Frames)
                keys.push_back(cFrame.Key);
        return keys;
    };
    CHECK(cKeysTo(kPeer) == std::vector{kLadle});
    CHECK(cKeysTo(kOtherPeer) == std::vector{cBucket});

    session.Moving.Rested = {{.Key = kLadle, .Position = {400.f, 0.f, 0.f}}};
    session.Frame(1100);
    CHECK(session.SentTo<NotifyBodyRests>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyBodyRests>(kOtherPeer).size() == 1);
}

TEST_CASE("Physics: a guest's own ragdoll goes to every other game; another's ragdoll from it does not", "[HostAuthority][Physics]")
{
    Session session;
    const uint32_t cOwn = session.Character(kPeer);
    const uint32_t cOther = session.Character(kOtherPeer);
    BodyFramesRequest frames;
    frames.Tick = 1000;
    frames.Frames = {{.Key = {.Actor = cOwn, .Index = 2}, .Position = {5.f, 0.f, 80.f}},
                     {.Key = {.Actor = cOther, .Index = 2}, .Position = {9.f, 0.f, 80.f}}};
    session.Receive(kPeer, frames);
    REQUIRE(session.SentTo<NotifyBodyFrames>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyBodyFrames>(kHost)[0]->Frames.size() == 1);
    CHECK(session.SentTo<NotifyBodyFrames>(kHost)[0]->Frames[0].Key.Actor == cOwn);
    CHECK_FALSE(session.SentTo<NotifyBodyFrames>(kHost)[0]->Held);
    CHECK(session.SentTo<NotifyBodyFrames>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyBodyFrames>(kPeer).empty());

    BodyRestsRequest rests;
    rests.Tick = 1200;
    rests.Rests = {{.Key = {.Actor = cOwn, .Index = 2}, .Position = {6.f, 0.f, 10.f}},
                   {.Key = {.Actor = cOther, .Index = 2}, .Position = {9.f, 0.f, 10.f}},
                   {.Key = kLadle, .Position = {1.f, 0.f, 10.f}}};
    session.Receive(kPeer, rests);
    REQUIRE(session.SentTo<NotifyBodyRests>(kHost).size() == 1);
    // The ladle's (a first mover's, its frames unheard); a ragdoll's rest is its owner's record part (RecordKind::kRagdoll).
    CHECK(session.SentTo<NotifyBodyRests>(kHost)[0]->Rests == std::vector<BodyPose>{rests.Rests[2]});
    CHECK(session.SentTo<NotifyBodyRests>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyBodyRests>(kPeer).empty());
    CHECK(session.Freed.empty());
}

TEST_CASE("Physics: a host NPC's corpse a guest's player drags is its mover's, as an object", "[HostAuthority][Physics][corpse-drag]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);
    const BodyKey cBone{.Actor = cNpc, .Index = 1};
    BodyFramesRequest frames;
    frames.Tick = 1000;
    frames.Held = true;
    frames.Frames = {{.Key = cBone, .Position = {5.f, 0.f, 10.f}}};
    // Alive: its ragdoll is its animation's, nobody's to move (review of PR #91).
    session.Receive(kPeer, frames);
    CHECK(session.SentTo<NotifyBodyFrames>(kHost).empty());
    CHECK(session.SentTo<NotifyBodyFrames>(kOtherPeer).empty());
    session.Dead.insert(cNpc);
    session.Receive(kPeer, frames);
    // Every other game plays its frames, the host's own game too (its corpse steered to them).
    REQUIRE(session.SentTo<NotifyBodyFrames>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyBodyFrames>(kHost)[0]->Frames[0].Key == cBone);
    CHECK(session.SentTo<NotifyBodyFrames>(kOtherPeer).size() == 1);
    // The host's own reads of it while the guest moves it are not streamed.
    session.Outbox.clear();
    session.Moving.Awake = {{.Key = cBone, .Position = {0.f, 0.f, 10.f}}};
    session.Frame(1050);
    CHECK(session.SentTo<NotifyBodyFrames>(kPeer).empty());
    CHECK(session.SentTo<NotifyBodyFrames>(kOtherPeer).empty());
    // A second guest's frames of it lose to the first mover's claim.
    BodyFramesRequest second = frames;
    second.Frames[0].Position = {50.f, 0.f, 10.f};
    session.Receive(kOtherPeer, second);
    CHECK(session.SentTo<NotifyBodyFrames>(kHost).empty());
    // Let go (its fall still the mover's), then its rest goes to every other game: the host's game puts its corpse there.
    BodyReleaseRequest released;
    released.Tick = 1100;
    released.Frames = {{.Key = cBone, .Position = {6.f, 0.f, 8.f}}};
    session.Receive(kPeer, released);
    CHECK(session.Freed.empty()); // a corpse's bodies stay simulated: nothing for the host's game to free
    session.Outbox.clear();
    BodyRestsRequest rests;
    rests.Tick = 1200;
    rests.Rests = {{.Key = cBone, .Position = {6.f, 0.f, 0.f}}};
    session.Receive(kPeer, rests);
    REQUIRE(session.SentTo<NotifyBodyRests>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyBodyRests>(kHost)[0]->Rests == rests.Rests);
    CHECK(session.SentTo<NotifyBodyRests>(kOtherPeer).size() == 1);

    // Revived during a drag: its frames and rest are refused from then on.
    session.Outbox.clear();
    session.Dead.erase(cNpc);
    frames.Tick = 1300;
    session.Receive(kPeer, frames);
    rests.Tick = 1400;
    session.Receive(kPeer, rests);
    CHECK(session.SentTo<NotifyBodyFrames>(kHost).empty());
    CHECK(session.SentTo<NotifyBodyRests>(kHost).empty());
}

TEST_CASE("Physics: a reliable release survives lost held frames and rejects older input", "[HostAuthority][Physics][release-order]")
{
    Session session;
    BodyReleaseRequest released;
    released.Tick = 1100;
    released.Frames = {{.Key = kLadle, .Position = {15.f, 0.f, 100.f}, .LinearVelocity = {200.f, 0.f, 50.f}}};
    SECTION("every held frame was lost")
    {
        session.Receive(kPeer, released);
        REQUIRE(session.Freed.size() == 1);
        CHECK(session.Freed[0] == released.Frames[0].Quantized());
        session.Outbox.clear();
        session.Receive(kPeer, Held(1050, 10.f));
        CHECK(session.Outbox.empty());
        session.Receive(kPeer, released);
        CHECK(session.Freed.size() == 1);
    }
    SECTION("a new grab reached the host before an older release")
    {
        session.Receive(kPeer, Held(1200, 25.f));
        session.Receive(kPeer, released);
        CHECK(session.Freed.empty());
    }
}

TEST_CASE("Physics: a guest's rest whose frames never reached the host is taken", "[HostAuthority][Physics]")
{
    // Codex review of #61: a push's claim is its unreliable frames; lost or late, the reliable rest was refused and
    // the guest's object snapped back to the host's pose.
    Session session;
    session.Bodies = {{.Key = kLadle, .Position = {5.f, 0.f, 0.f}}};
    SECTION("no frame at all")
    {
        session.Receive(kPeer, RestsAt(1100, 20.f));
        REQUIRE(session.SentTo<NotifyBodyRests>(kHost).size() == 1);
        CHECK(session.SentTo<NotifyBodyRests>(kHost).back()->Rests[0].Position.x == 20.f);
        CHECK(session.SentTo<NotifyBodyRests>(kPeer).empty());
    }
    SECTION("its frames after its rest")
    {
        session.Receive(kPeer, RestsAt(1100, 20.f));
        session.Outbox.clear();
        session.Receive(kPeer, Moved(1050, 15.f));
        CHECK(session.SentTo<NotifyBodyRests>(kPeer).empty());
    }
}

TEST_CASE("Physics: a second mover's rest after the first's rested gets the host's pose back", "[HostAuthority][Physics]")
{
    // Two guests pushed one body: the first's claim won and its rest was placed; the second's own rest is refused.
    Session session;
    session.Receive(kPeer, Moved(1000, 10.f));
    session.Receive(kOtherPeer, Moved(1010, 40.f));
    session.Receive(kPeer, RestsAt(1100, 20.f));
    session.Bodies = {{.Key = kLadle, .Position = {20.f, 0.f, 0.f}}};
    session.Outbox.clear();
    session.Receive(kOtherPeer, RestsAt(1200, 45.f));
    CHECK(session.SentTo<NotifyBodyRests>(kHost).empty());
    REQUIRE(session.SentTo<NotifyBodyRests>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyBodyRests>(kOtherPeer).back()->Rests[0].Position.x == 20.f);
}

TEST_CASE("Physics: the host's game placing a body a guest moves takes it back", "[HostAuthority][Physics]")
{
    // The duo, 2026-11-15: the host's console moved a cabbage a guest's player had just pushed; the host's game kept
    // following the guest's frames, and the cabbage stayed where the guest had it.
    Session session;
    session.Receive(kPeer, Moved(1000, 10.f));
    session.Outbox.clear();

    SECTION("put elsewhere by the host's game: the mover gets the host's pose, its later frames are dropped")
    {
        session.Bodies = {{.Key = kLadle, .Position = {300.f, 0.f, 0.f}}};
        session.Placed = {{kLadle.Object, session.Bodies[0].Position}};
        session.Frame(1010);
        REQUIRE(session.SentTo<NotifyBodyRests>(kPeer).size() == 1);
        CHECK(session.SentTo<NotifyBodyRests>(kPeer).back()->Placed);
        CHECK(session.SentTo<NotifyBodyRests>(kPeer).back()->Rests[0].Position.x == 300.f);
        REQUIRE(session.Freed.size() == 1);
        session.Outbox.clear();
        session.Receive(kPeer, Moved(1050, 12.f));
        CHECK(session.SentTo<NotifyBodyFrames>(kHost).empty());
        // Placed again (the console moves an axis at a time): the mover gets that place too.
        session.Bodies = {{.Key = kLadle, .Position = {300.f, 80.f, 0.f}}};
        session.Placed = {{kLadle.Object, session.Bodies[0].Position}};
        session.Frame(1020);
        REQUIRE(session.SentTo<NotifyBodyRests>(kPeer).size() == 1);
        CHECK(session.SentTo<NotifyBodyRests>(kPeer).back()->Rests[0].Position.y == 80.f);
    }
    SECTION("a move row of the body where the mover's frame put it is not the host's game's")
    {
        session.Bodies = {{.Key = kLadle, .Position = {11.f, 0.f, 100.f}}};
        session.Placed = {{kLadle.Object, session.Bodies[0].Position}};
        session.Frame(1010);
        CHECK(session.SentTo<NotifyBodyRests>(kPeer).empty());
        session.Receive(kPeer, Moved(1050, 12.f));
        CHECK(!session.SentTo<NotifyBodyFrames>(kHost).empty());
    }
}

TEST_CASE("Physics: the host's game placing a body a guest rested drops that rest", "[HostAuthority][Physics]")
{
    // The duo, 2026-11-15 (tavern staging): a guest's rest the host's game was still putting (a pile's waits for its
    // island to sleep) put the object back after the console placed it on the lane; staging timed out.
    Session session;
    session.Receive(kPeer, Moved(1000, 10.f));
    session.Receive(kPeer, RestsAt(1010, 12.f));
    session.Freed.clear();
    session.Outbox.clear();
    session.Bodies = {{.Key = kLadle, .Position = {300.f, 0.f, 0.f}}};
    session.Placed = {{kLadle.Object, session.Bodies[0].Position}};
    session.Frame(1020);
    REQUIRE(session.Freed.size() == 1);
    CHECK(session.Freed[0].Position.x == 300.f);
    // Its mover gives it back (GuestHands::GiveBack).
    REQUIRE(session.SentTo<NotifyBodyRests>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyBodyRests>(kPeer).back()->Placed);
}

TEST_CASE("Physics: a body a guest's own physics moved is followed on the host from then", "[HostAuthority][Physics]")
{
    // The duo, 2026-11-15: an item leaning on the host's player stood there, awake and unmoved (no frames), and fell
    // in the guest's game, where a copy pushes nothing. The guest asked, was put back, fell, asked: once a second.
    Session session;
    session.Bodies = {{.Key = kLadle, .Position = {5.f, 0.f, 0.f}}};
    BodyPoseRequest asked;
    asked.Rests = {{.Key = kLadle}};
    session.Receive(kPeer, asked);
    REQUIRE(session.SentTo<NotifyBodyRests>(kPeer).size() == 1);
    REQUIRE(session.Followed.size() == 1);
    CHECK(session.Followed[0] == kLadle);
}
