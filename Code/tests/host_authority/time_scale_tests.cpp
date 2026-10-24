// HostTimeScale: a game slowing its clock (a kill cam, Slow Time) slows everyone's.
#include "session.h"

#include <Messages/NotifyTimeScale.h>
#include <Messages/TimeScaleRequest.h>

namespace
{
TimeScaleRequest Slow(float aMultiplier)
{
    TimeScaleRequest request;
    request.Multiplier = aMultiplier;
    return request;
}

float LastScale(const Session& acSession, ConnectionId_t aPlayer)
{
    const auto cSent = acSession.SentTo<NotifyTimeScale>(aPlayer);
    REQUIRE(!cSent.empty());
    return cSent.back()->Multiplier;
}
} // namespace

TEST_CASE("Time scale: a player's slow clock slows the others", "[HostAuthority][TimeScale]")
{
    Session session;

    session.Receive(kPeer, Slow(0.25f));
    CHECK(LastScale(session, kHost) == 0.25f);
    CHECK(LastScale(session, kOtherPeer) == 0.25f);
    // Its own game already runs at its own speed.
    CHECK(session.SentTo<NotifyTimeScale>(kPeer).empty());

    // Ramping back: each step reaches the others.
    session.Receive(kPeer, Slow(0.6f));
    CHECK(LastScale(session, kHost) == 0.6f);
    session.Receive(kPeer, Slow(1.f));
    CHECK(LastScale(session, kHost) == 1.f);
    CHECK(LastScale(session, kOtherPeer) == 1.f);
}

TEST_CASE("Time scale: two slow clocks, the slowest wins", "[HostAuthority][TimeScale]")
{
    Session session;

    session.Receive(kHost, Slow(0.5f));
    session.Receive(kPeer, Slow(0.25f));
    CHECK(LastScale(session, kOtherPeer) == 0.25f);
    CHECK(LastScale(session, kHost) == 0.25f);
    // The peer runs no faster than the host's 0.5 (its own 0.25 is slower already: told 0.5).
    CHECK(LastScale(session, kPeer) == 0.5f);

    // The peer's kill cam ends: the host's slow clock still holds the others.
    session.Outbox.clear();
    session.Receive(kPeer, Slow(1.f));
    CHECK(LastScale(session, kOtherPeer) == 0.5f);
    CHECK(LastScale(session, kHost) == 1.f);
    // Nothing changed for the peer itself: not sent again.
    CHECK(session.SentTo<NotifyTimeScale>(kPeer).empty());
}

TEST_CASE("Time scale: a player leaving mid kill cam frees the others' clocks", "[HostAuthority][TimeScale]")
{
    Session session;

    session.Receive(kPeer, Slow(0.25f));
    session.Leave(kPeer);
    CHECK(LastScale(session, kHost) == 1.f);
    CHECK(LastScale(session, kOtherPeer) == 1.f);
}

TEST_CASE("Time scale: a joining player gets the slowed clock", "[HostAuthority][TimeScale]")
{
    Session session;
    session.Receive(kHost, Slow(0.25f));
    session.Outbox.clear();

    AnnounceCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    announce.SessionId = session.CharacterId(kPeer);
    session.Receive(kPeer, announce);
    CHECK(LastScale(session, kPeer) == 0.25f);

    // And its clock goes back when the slowdown ends.
    session.Outbox.clear();
    session.Receive(kHost, Slow(1.f));
    CHECK(LastScale(session, kPeer) == 1.f);
}

TEST_CASE("Time scale: a player joining mid slowdown is freed when it ends", "[HostAuthority][TimeScale]")
{
    Session session;
    session.Receive(kHost, Slow(0.25f));
    session.Leave(kPeer);
    session.Join(kPeer, JoinRequest("Lydia", 12));
    AnnounceCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    announce.SessionId = session.CharacterId(kPeer);
    session.Receive(kPeer, announce);
    CHECK(LastScale(session, kPeer) == 0.25f);

    session.Outbox.clear();
    session.Receive(kHost, Slow(1.f));
    CHECK(LastScale(session, kPeer) == 1.f);
}
