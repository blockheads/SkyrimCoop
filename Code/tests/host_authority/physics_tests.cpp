// HostBodies: the host arbitrates who simulates each body, after Fiedler (docs/design/physics-authority-plan.md).
#include "session.h"

namespace
{
const BodyKey kLadle{.Object = GameId(0, 0x0001A0F3)};
const BodyKey kBucket{.Object = GameId(0, 0x0001A0F4)};

// A guest's state of the ladle, from its BodyAuthority's numbers.
BodyStatesRequest StateOf(uint64_t aTick, float aX, uint16_t aAuthSeq, uint16_t aHoldSeq = 0, bool aHeld = false, bool aAtRest = false,
                          const BodyKey& acKey = kLadle)
{
    BodyStatesRequest request;
    request.Tick = aTick;
    request.States = {{.Frame = {.Key = acKey, .Position = {aX, 0.f, 100.f}, .LinearVelocity = {aAtRest ? 0.f : 50.f, 0.f, 0.f}},
                       .Author = BodyAuthor::kYou, .AuthSeq = aAuthSeq, .HoldSeq = aHoldSeq, .Held = aHeld, .AtRest = aAtRest}};
    return request;
}

// The newest state of that body a game got, if any.
std::optional<BodyState> LastTo(const Session& acSession, ConnectionId_t aGame, const BodyKey& acKey = kLadle)
{
    std::optional<BodyState> last;
    for (const auto* pStates : acSession.SentTo<NotifyBodyStates>(aGame))
        for (const auto& cState : pStates->States)
            if (cState.Frame.Key == acKey)
                last = cState;
    return last;
}
} // namespace

TEST_CASE("Physics: a guest's player holds a body, its alone: every other game gets the holder's states", "[HostAuthority][Physics]")
{
    Session session;
    session.Receive(kPeer, StateOf(1000, 10.f, 0, 1, true));
    session.Frame(1016);
    THEN("the host's game and the other guest get it held by another; the holder gets none back")
    {
        for (const auto cGame : {kHost, kOtherPeer})
        {
            const auto cState = LastTo(session, cGame);
            REQUIRE(cState);
            CHECK(cState->Held);
            CHECK(cState->Author == BodyAuthor::kOther);
            CHECK(cState->Frame.Position.x == Approx(10.f).margin(0.05f));
        }
        CHECK_FALSE(LastTo(session, kPeer));
    }
    THEN("another guest's state of it does not stand, whatever its numbers")
    {
        session.Outbox.clear();
        session.Receive(kOtherPeer, StateOf(1100, 40.f, 9, 0));
        session.Receive(kOtherPeer, StateOf(1110, 40.f, 0, 5, true));
        session.Frame(1600);
        const auto cState = LastTo(session, kHost);
        REQUIRE(cState);
        CHECK(cState->Frame.Position.x == Approx(10.f).margin(0.05f));
    }
}

TEST_CASE("Physics: the host's player holds a body: each guest gets it held, the others free and nobody's", "[HostAuthority][Physics]")
{
    Session session;
    session.Moving.Awake = {{.Key = kLadle, .Position = {100.f, 0.f, 0.f}, .LinearVelocity = {300.f, 0.f, 0.f}},
                            {.Key = kBucket, .Position = {120.f, 0.f, 0.f}, .LinearVelocity = {300.f, 0.f, 0.f}}};
    session.Moving.Held = {kLadle};
    session.Frame(1000);
    for (const auto cGuest : {kPeer, kOtherPeer})
    {
        const auto cHeld = LastTo(session, cGuest, kLadle);
        const auto cFree = LastTo(session, cGuest, kBucket);
        REQUIRE(cHeld);
        REQUIRE(cFree);
        CHECK(cHeld->Held);
        CHECK(cHeld->Author == BodyAuthor::kOther);
        CHECK_FALSE(cFree->Held);
        CHECK(cFree->Author == BodyAuthor::kNone);
    }
    THEN("the host's game gets no state of what it simulates itself") CHECK(session.SentTo<NotifyBodyStates>(kHost).empty());
}

TEST_CASE("Physics: a guest's let go stays its until it rests; a later touch by another takes it (the last toucher)", "[HostAuthority][Physics]")
{
    Session session;
    session.Receive(kPeer, StateOf(1000, 10.f, 0, 1, true));
    session.Receive(kPeer, StateOf(1100, 15.f, 0, 1, false));
    session.Frame(1116);
    REQUIRE(LastTo(session, kHost));
    CHECK_FALSE(LastTo(session, kHost)->Held);
    CHECK(LastTo(session, kHost)->Author == BodyAuthor::kOther);
    THEN("another guest touching it takes it by a newer number")
    {
        session.Outbox.clear();
        session.Receive(kOtherPeer, StateOf(1200, 30.f, 1, 1));
        session.Frame(1216);
        REQUIRE(LastTo(session, kPeer));
        CHECK(LastTo(session, kPeer)->Author == BodyAuthor::kOther);
        CHECK(LastTo(session, kPeer)->Frame.Position.x == Approx(30.f).margin(0.05f));
        THEN("the first guest's older states no longer stand")
        {
            session.Outbox.clear();
            session.Receive(kPeer, StateOf(1220, 99.f, 0, 1));
            session.Frame(1800);
            CHECK(LastTo(session, kHost)->Frame.Position.x == Approx(30.f).margin(1.f));
        }
    }
}

TEST_CASE("Physics: two guests touching one body at once: the first heard keeps it, the other is told it is another's", "[HostAuthority][Physics]")
{
    Session session;
    session.Receive(kPeer, StateOf(1000, 10.f, 1));
    session.Receive(kOtherPeer, StateOf(1000, 12.f, 1));
    session.Frame(1016);
    REQUIRE(LastTo(session, kOtherPeer));
    CHECK(LastTo(session, kOtherPeer)->Author == BodyAuthor::kOther);
    CHECK(LastTo(session, kOtherPeer)->AuthSeq == 1);
    CHECK(LastTo(session, kOtherPeer)->Frame.Position.x == Approx(10.f).margin(0.05f));
}

TEST_CASE("Physics: a guest's rest gives the body back to nobody: the host's game puts it, the mover hears it confirmed", "[HostAuthority][Physics]")
{
    Session session;
    session.Receive(kPeer, StateOf(1000, 10.f, 1));
    session.Frame(1016);
    session.Outbox.clear();
    session.Receive(kPeer, StateOf(1300, 20.f, 2, 0, false, true));
    session.Frame(1316);
    for (const auto cGame : {kHost, kPeer, kOtherPeer})
    {
        const auto cState = LastTo(session, cGame);
        REQUIRE(cState);
        CHECK(cState->AtRest);
        CHECK(cState->Author == BodyAuthor::kNone);
        CHECK(cState->AuthSeq == 2);
        CHECK(cState->Frame.Position.x == 20.f);
    }
    THEN("the host's game putting it there (awake a step, then at rest within the send limits) is not its own move")
    {
        session.Outbox.clear();
        session.Moving.Awake = {{.Key = kLadle, .Position = {20.5f, 0.f, 100.f}, .LinearVelocity = {1.f, 0.f, 0.f}}};
        session.Frame(1332);
        session.Moving.Rested = {{.Key = kLadle, .Position = {20.6f, 0.f, 100.f}}};
        session.Frame(1348);
        for (const auto* pStates : session.SentTo<NotifyBodyStates>(kPeer))
            for (const auto& cState : pStates->States)
                CHECK((cState.AtRest && cState.Frame.Position.x == 20.f));
        THEN("the host's game gets that same rest at most (repeats of a change, a lost one's repair)")
            if (const auto cState = LastTo(session, kHost))
                CHECK(cState->Frame.Position.x == 20.f);
    }
    THEN("the resting body comes round again on the stream")
    {
        session.Outbox.clear();
        session.Frame(1316 + BodyTolerances::kRestRefreshMs + 100);
        CHECK(LastTo(session, kOtherPeer));
    }
}

TEST_CASE("Physics: a guest's rest the host's game read still before it got it goes to the host's game all the same", "[HostAuthority][Physics][guest-rest]")
{
    // guest-push-like-vanilla, 2026-11-23: without it the host's game steered the ladle to the last state for 2 s.
    Session session;
    session.Receive(kPeer, StateOf(1000, 10.f, 1));
    session.Frame(1016);
    session.Outbox.clear();
    session.Receive(kPeer, StateOf(1300, 20.f, 2, 0, false, true));
    const float cWhere = GENERATE(20.6f, 15.f); // on it, or short of it (shown behind)
    session.Moving.Rested = {{.Key = kLadle, .Position = {cWhere, 0.f, 100.f}}};
    session.Frame(1316);
    REQUIRE(LastTo(session, kHost));
    CHECK(LastTo(session, kHost)->AtRest);
    CHECK(LastTo(session, kHost)->Frame.Position.x == 20.f);
    CHECK(LastTo(session, kPeer)->Frame.Position.x == 20.f);
}

TEST_CASE("Physics: the host's game placing a body, or its player touching it, ends a guest's rest being put there", "[HostAuthority][Physics][guest-rest]")
{
    Session session;
    session.Receive(kPeer, StateOf(1000, 10.f, 1));
    session.Frame(1016);
    session.Receive(kPeer, StateOf(1300, 20.f, 2, 0, false, true));
    session.Frame(1316);
    REQUIRE(session.Ended.empty());
    SECTION("placed")
    {
        session.Bodies = {{.Key = kLadle, .Position = {500.f, 0.f, 0.f}}};
        session.Placed = {{kLadle.Object, {500.f, 0.f, 0.f}}};
    }
    SECTION("touched")
    {
        session.Touches = {kLadle};
        session.Moving.Awake = {{.Key = kLadle, .Position = {24.f, 0.f, 100.f}, .LinearVelocity = {80.f, 0.f, 0.f}}};
    }
    session.Outbox.clear();
    session.Frame(1332);
    CHECK(session.Ended == std::vector{kLadle});
    REQUIRE(LastTo(session, kPeer));
    CHECK(LastTo(session, kPeer)->Frame.Position.x > 23.f);
}

TEST_CASE("Physics: a guest's rest stamped ahead of the host's clock still goes to the host's game", "[HostAuthority][Physics][guest-rest]")
{
    Session session;
    session.Receive(kPeer, StateOf(1000, 10.f, 1));
    session.Frame(1016);
    session.Outbox.clear();
    session.Receive(kPeer, StateOf(1033, 20.f, 2, 0, false, true));
    session.Frame(1032);
    REQUIRE(LastTo(session, kHost));
    CHECK(LastTo(session, kHost)->AtRest);
}

TEST_CASE("Physics: the host's game placing a sleeping body a little off, or turning it, goes to every guest", "[HostAuthority][Physics][placed]")
{
    Session session;
    session.Moving.Rested = {{.Key = kLadle, .Position = {10.f, 0.f, 100.f}}};
    session.Frame(1000);
    session.Outbox.clear();
    BodyPose now{.Key = kLadle, .Position = {10.f, 0.f, 100.f}};
    SECTION("moved 8 units") now.Position.x = 18.f;
    SECTION("turned in place") now.Rotation = glm::angleAxis(0.5f, glm::vec3(0.f, 0.f, 1.f));
    session.Bodies = {now};
    session.Placed = {{kLadle.Object, now.Position}};
    session.Frame(1016);
    REQUIRE(LastTo(session, kPeer));
    CHECK(LastTo(session, kPeer)->Placed);
    CHECK(LastTo(session, kPeer)->Frame.Position == now.Position);
    CHECK(std::abs(glm::dot(LastTo(session, kPeer)->Frame.Rotation, now.Rotation)) > 0.9999f);
}

TEST_CASE("Physics: a moving body the host's game no longer reads is read where it is, at rest, or forgotten", "[HostAuthority][Physics]")
{
    // Streamed on stale, every game dropped it as lost on arrival.
    Session session;
    session.Moving.Awake = {{.Key = kLadle, .Position = {10.f, 0.f, 100.f}, .LinearVelocity = {50.f, 0.f, 0.f}}};
    session.Frame(1000);
    session.Outbox.clear();
    SECTION("still there")
    {
        session.Bodies = {{.Key = kLadle, .Position = {30.f, 0.f, 100.f}}};
        session.Frame(3100);
        REQUIRE(LastTo(session, kPeer));
        CHECK(LastTo(session, kPeer)->AtRest);
        CHECK(LastTo(session, kPeer)->Frame.Position.x == 30.f);
    }
    SECTION("gone")
    {
        session.Frame(3100);
        CHECK_FALSE(LastTo(session, kPeer));
    }
}

TEST_CASE("Physics: an authority gone silent, or gone, gives its bodies back where they are", "[HostAuthority][Physics]")
{
    Session session;
    session.Receive(kPeer, StateOf(1000, 10.f, 0, 1, true));
    session.Frame(1016);
    THEN("one long frame is not silence")
    {
        session.Frame(1016 + BodyTolerances::kSilentMs + 1);
        CHECK(session.Ended.empty());
    }
    for (uint32_t i = 1; i <= HostBodies::kQuietUpdates + 1; ++i)
        session.Frame(1016 + BodyTolerances::kSilentMs + i);
    CHECK(session.Ended == std::vector{kLadle});
    REQUIRE(LastTo(session, kOtherPeer));
    CHECK(LastTo(session, kOtherPeer)->Author == BodyAuthor::kNone);
    CHECK_FALSE(LastTo(session, kOtherPeer)->Held);

    Session leaving;
    leaving.Receive(kPeer, StateOf(1000, 10.f, 1));
    leaving.Frame(1016);
    leaving.Leave(kPeer);
    CHECK(leaving.Ended == std::vector{kLadle});
}

TEST_CASE("Physics: a body a guest's physics moved that the host never sent: the host's state of it goes to that guest", "[HostAuthority][Physics]")
{
    Session session;
    session.Bodies = {{.Key = kLadle, .Position = {10.f, 20.f, 30.f}}};
    BodyStatesRequest hint;
    hint.Tick = 1000;
    hint.States = {{.Frame = {.Key = kLadle, .Position = {400.f, 20.f, 0.f}}, .Author = BodyAuthor::kNone, .AtRest = true},
                   {.Frame = {.Key = {.Object = GameId(0, 0x1234)}}, .Author = BodyAuthor::kNone, .AtRest = true}};
    session.Receive(kPeer, hint);
    session.Frame(1016);
    const auto cState = LastTo(session, kPeer);
    REQUIRE(cState);
    CHECK(cState->AtRest);
    CHECK(cState->Author == BodyAuthor::kNone);
    CHECK(cState->Frame.Position == glm::vec3(10.f, 20.f, 30.f));
    CHECK_FALSE(LastTo(session, kPeer, {.Object = GameId(0, 0x1234)}));
}

TEST_CASE("Physics: a guest gets the moving states of the bodies near its character, every guest the resting ones", "[HostAuthority][Physics]")
{
    Session session;
    session.PlayerPositions[session.PlayerId(kPeer)] = {0.f, 0.f, 0.f};
    session.PlayerPositions[session.PlayerId(kOtherPeer)] = {20000.f, 0.f, 0.f};
    session.Moving.Awake = {{.Key = kLadle, .Position = {100.f, 0.f, 0.f}, .LinearVelocity = {300.f, 0.f, 0.f}},
                            {.Key = kBucket, .Position = {19900.f, 0.f, 0.f}, .LinearVelocity = {300.f, 0.f, 0.f}}};
    session.Frame(1000);
    CHECK(LastTo(session, kPeer, kLadle));
    CHECK_FALSE(LastTo(session, kPeer, kBucket));
    CHECK(LastTo(session, kOtherPeer, kBucket));
    CHECK_FALSE(LastTo(session, kOtherPeer, kLadle));

    session.Outbox.clear();
    session.Moving.Rested = {{.Key = kLadle, .Position = {400.f, 0.f, 0.f}}};
    session.Frame(1100);
    CHECK(LastTo(session, kPeer, kLadle)->AtRest);
    CHECK(LastTo(session, kOtherPeer, kLadle)->AtRest);
}

TEST_CASE("Physics: a guest's own ragdoll goes to every other game; another's ragdoll from it does not", "[HostAuthority][Physics]")
{
    Session session;
    const BodyKey cOwn{.Actor = session.Character(kPeer), .Index = 2};
    const BodyKey cOther{.Actor = session.Character(kOtherPeer), .Index = 2};
    BodyStatesRequest states;
    states.Tick = 1000;
    states.States = {{.Frame = {.Key = cOwn, .Position = {5.f, 0.f, 80.f}}, .Author = BodyAuthor::kYou, .AuthSeq = 1},
                     {.Frame = {.Key = cOther, .Position = {9.f, 0.f, 80.f}}, .Author = BodyAuthor::kYou, .AuthSeq = 1}};
    session.Receive(kPeer, states);
    session.Frame(1016);
    for (const auto cGame : {kHost, kOtherPeer})
    {
        CHECK(LastTo(session, cGame, cOwn));
        CHECK_FALSE(LastTo(session, cGame, cOther));
    }
    CHECK_FALSE(LastTo(session, kPeer, cOwn));
}

TEST_CASE("Physics: a host NPC's corpse a guest's player grabs is that guest's; a living NPC's ragdoll is not", "[HostAuthority][Physics][corpse-drag]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);
    const BodyKey cBody{.Actor = cNpc, .Index = 0};
    session.Receive(kPeer, StateOf(1000, 5.f, 0, 1, true, false, cBody));
    session.Frame(1016);
    CHECK_FALSE(LastTo(session, kHost, cBody));
    session.Dead.insert(cNpc);
    session.Receive(kPeer, StateOf(1100, 6.f, 0, 1, true, false, cBody));
    session.Frame(1116);
    REQUIRE(LastTo(session, kHost, cBody));
    CHECK(LastTo(session, kHost, cBody)->Held);
}

TEST_CASE("Physics: the host's game placing a body a guest moves takes it back by a newer number; a held one stays", "[HostAuthority][Physics]")
{
    Session session;
    session.Bodies = {{.Key = kLadle, .Position = {500.f, 0.f, 0.f}}};
    session.Receive(kPeer, StateOf(1000, 10.f, 1));
    session.Frame(1016);
    session.Outbox.clear();
    session.Placed = {{kLadle.Object, {500.f, 0.f, 0.f}}};
    session.Frame(1032);
    CHECK(session.Ended == std::vector{kLadle});
    const auto cState = LastTo(session, kPeer);
    REQUIRE(cState);
    CHECK(cState->Author == BodyAuthor::kNone);
    CHECK(cState->AtRest);
    CHECK(cState->AuthSeq == 2);
    CHECK(cState->Frame.Position.x == 500.f);
    THEN("the mover's states from before it heard do not stand")
    {
        session.Outbox.clear();
        session.Receive(kPeer, StateOf(1040, 12.f, 1));
        session.Frame(1048);
        CHECK(LastTo(session, kHost) == std::nullopt);
    }

    Session holding;
    holding.Receive(kPeer, StateOf(1000, 10.f, 0, 1, true));
    holding.Frame(1016);
    holding.Placed = {{kLadle.Object, {500.f, 0.f, 0.f}}};
    holding.Frame(1032);
    CHECK(holding.Ended.empty());
}

TEST_CASE("Physics: the host's player touching a guest's body takes it: its game simulates it again", "[HostAuthority][Physics]")
{
    Session session;
    session.Receive(kPeer, StateOf(1000, 10.f, 1));
    session.Frame(1016);
    session.Outbox.clear();
    session.Touches = {kLadle};
    session.Moving.Awake = {{.Key = kLadle, .Position = {14.f, 0.f, 100.f}, .LinearVelocity = {80.f, 0.f, 0.f}}};
    session.Frame(1032);
    CHECK(session.Ended == std::vector{kLadle});
    REQUIRE(LastTo(session, kPeer));
    CHECK(LastTo(session, kPeer)->Author == BodyAuthor::kOther);
    CHECK(LastTo(session, kPeer)->AuthSeq == 2);
    CHECK(LastTo(session, kPeer)->Frame.Position.x == Approx(14.f).margin(0.05f));
    THEN("what its body touches with nobody's claim is the host player's too")
    {
        // Paired as their AABBs meet (64648); the bucket is taken once it leaves where it lay.
        session.Contacts = {{kLadle, kBucket}};
        session.Bodies = {{.Key = kBucket, .Position = {30.f, 0.f, 100.f}}};
        session.Moving.Awake = {{.Key = kLadle, .Position = {16.f, 0.f, 100.f}, .LinearVelocity = {80.f, 0.f, 0.f}}};
        session.Frame(1048);
        CHECK(LastTo(session, kPeer, kBucket) == std::nullopt);
        session.Bodies = {{.Key = kBucket, .Position = {32.f, 0.f, 100.f}}};
        session.Moving.Awake = {{.Key = kLadle, .Position = {18.f, 0.f, 100.f}, .LinearVelocity = {80.f, 0.f, 0.f}},
                                {.Key = kBucket, .Position = {32.f, 0.f, 100.f}, .LinearVelocity = {40.f, 0.f, 0.f}}};
        session.Frame(1064);
        REQUIRE(LastTo(session, kPeer, kBucket));
        CHECK(LastTo(session, kPeer, kBucket)->Author == BodyAuthor::kOther);
        THEN("and a guest's state of it from before does not stand")
        {
            session.Outbox.clear();
            session.Receive(kPeer, StateOf(1070, 50.f, 1, 0, false, false, kBucket));
            session.Frame(1600);
            CHECK(LastTo(session, kPeer, kBucket)->Author == BodyAuthor::kOther);
        }
    }
}
