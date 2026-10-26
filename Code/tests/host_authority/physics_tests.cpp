// HostHands: a guest's player's hand on the host's bodies (docs/design/physics-sync.md, "A guest's own actions").
#include "session.h"

namespace
{
const BodyKey kLadle{.Object = GameId(0, 0x0001A0F3)};

BodyFramesRequest Held(uint64_t aTick, float aX)
{
    BodyFramesRequest held;
    held.Tick = aTick;
    held.Frames = {{.Key = kLadle, .Position = {aX, 0.f, 100.f}, .LinearVelocity = {50.f, 0.f, 0.f}}};
    return held;
}
} // namespace

TEST_CASE("Physics: a guest's push pushes the host's body, unless a player holds it", "[HostAuthority][Physics]")
{
    Session session;
    BodyPushRequest pushes;
    pushes.Pushes = {{.Key = kLadle, .Impulse = {12.f, 0.f, 0.f}, .Point = {1.f, 2.f, 3.f}}};
    session.Receive(kPeer, pushes);
    REQUIRE(session.Pushed.size() == 1);
    CHECK(session.Pushed[0] == pushes.Pushes[0]);

    session.Receive(kOtherPeer, Held(1000, 10.f));
    session.Receive(kPeer, pushes);
    CHECK(session.Pushed.size() == 1);
}

TEST_CASE("Physics: a guest's player holds a body, the host's follows it and the others see it", "[HostAuthority][Physics]")
{
    Session session;
    session.Receive(kPeer, Held(1000, 10.f));
    session.Receive(kPeer, Held(1050, 12.5f));
    session.Frame(1160);
    REQUIRE_FALSE(session.Driven.empty());
    CHECK(session.Driven.back().Key == kLadle);
    CHECK(session.Driven.back().Position.x == Approx(12.5f + 50.f * 0.01f));
    THEN("the other guest gets the holder's frames, the holder and the host none")
    {
        CHECK(session.SentTo<NotifyBodyFrames>(kOtherPeer).size() == 2);
        CHECK(session.SentTo<NotifyBodyFrames>(kPeer).empty());
        CHECK(session.SentTo<NotifyBodyFrames>(kHost).empty());
    }
    THEN("a second guest's hold of it is not taken")
    {
        session.Outbox.clear();
        session.Receive(kOtherPeer, Held(1100, 40.f));
        CHECK(session.SentTo<NotifyBodyFrames>(kPeer).empty());
    }
}

TEST_CASE("Physics: a guest's player lets go, the host simulates it from there", "[HostAuthority][Physics]")
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
        session.Receive(kPeer, Held(1090, 14.f));
        session.Frame(1300);
        CHECK(session.Driven.empty());
    }
    THEN("anyone may hold it again")
    {
        session.Receive(kOtherPeer, Held(1200, 30.f));
        session.Frame(1400);
        CHECK_FALSE(session.Driven.empty());
    }
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
    BodyRestRequest asked;
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
