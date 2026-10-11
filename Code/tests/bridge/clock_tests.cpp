// HostClock: the session's time is the host's clock.
#include "session.h"

TEST_CASE("Time: a joining player gets the host's clock", "[Bridge][Time]")
{
    Session session;
    session.HostClock.Time = 15.5f;
    session.HostClock.Day = 17;

    AssignCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    session.Receive(kPeer, announce);
    const auto cTimes = session.SentTo<ServerTimeSettings>(kPeer);
    REQUIRE(cTimes.size() == 1);
    CHECK(cTimes[0]->timeModel == session.HostClock);
}

TEST_CASE("Time: peers follow the host's clock when it jumps, not while it runs", "[Bridge][Time]")
{
    Session session;
    session.HostClock.TimeScale = 20.f;
    session.HostClock.Time = 10.f;
    session.Frame(1000);
    REQUIRE(session.SentTo<ServerTimeSettings>(kPeer).size() == 1);
    CHECK(session.SentTo<ServerTimeSettings>(kHost).empty());

    // A real minute at 20x: 20 game minutes, as peers run it themselves.
    session.Outbox.clear();
    session.HostClock.Time = 10.f + 20.f / 60.f;
    session.Frame(61000);
    CHECK(session.Outbox.empty());

    // Waiting 6 hours.
    session.HostClock.Time += 6.f;
    session.Frame(62000);
    REQUIRE(session.SentTo<ServerTimeSettings>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<ServerTimeSettings>(kOtherPeer)[0]->timeModel.Time == session.HostClock.Time);

    // Sleeping a whole day lands on the same hour: still a jump.
    session.Outbox.clear();
    session.HostClock.Day += 1;
    session.Frame(62000);
    CHECK(session.SentTo<ServerTimeSettings>(kPeer).size() == 1);

    // The clock's speed changing.
    session.Outbox.clear();
    session.HostClock.TimeScale = 6.f;
    session.Frame(62000);
    CHECK(session.SentTo<ServerTimeSettings>(kPeer).size() == 1);
}

TEST_CASE("Set time: any player's sets the host's clock", "[Bridge][Time]")
{
    Session session;
    SetTimeCommandRequest command;
    command.Hours = 21;
    command.Minutes = 30;
    session.Receive(kPeer, command);
    REQUIRE(session.SentTo<NotifySetTimeResult>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifySetTimeResult>(kPeer)[0]->Result == NotifySetTimeResult::SetTimeResult::kSuccess);
    CHECK(session.HostClock.Time == Approx(21.5f));

    // Peers then follow it like any jump of the host's clock.
    session.Frame(1000);
    REQUIRE(session.SentTo<ServerTimeSettings>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<ServerTimeSettings>(kOtherPeer)[0]->timeModel.Time == Approx(21.5f));

    session.Outbox.clear();
    command.Hours = 24; // not an hour
    session.Receive(kHost, command);
    CHECK(session.SentTo<NotifySetTimeResult>(kHost)[0]->Result == NotifySetTimeResult::SetTimeResult::kNoPermission);
    CHECK(session.HostClock.Time == Approx(21.5f));
}
