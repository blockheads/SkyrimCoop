// HostClock: the session's time is the host's clock.
#include "session.h"

#include <limits>

TEST_CASE("Time: a joining player gets the host's clock", "[HostAuthority][Time]")
{
    Session session;
    session.HostClock.Time = 15.5f;
    session.HostClock.Day = 17;
    session.HostClock.DaysPassed = 300;

    AnnounceCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    announce.SessionId = session.CharacterId(kPeer);
    session.Receive(kPeer, announce);
    const auto cTimes = session.SentTo<NotifyTimeSettings>(kPeer);
    REQUIRE(cTimes.size() == 1);
    CHECK(cTimes[0]->timeModel == session.HostClock);
}

TEST_CASE("Time: peers follow the host's clock when it jumps, not while it runs", "[HostAuthority][Time]")
{
    Session session;
    session.HostClock.TimeScale = 20.f;
    session.HostClock.Time = 10.f;
    session.Frame(1000);
    REQUIRE(session.SentTo<NotifyTimeSettings>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyTimeSettings>(kHost).empty());

    // A real minute at 20x: 20 game minutes, as peers run it themselves.
    session.Outbox.clear();
    session.HostClock.Time = 10.f + 20.f / 60.f;
    session.Frame(61000);
    CHECK(session.Outbox.empty());

    // Waiting 6 hours.
    session.HostClock.Time += 6.f;
    session.Frame(62000);
    REQUIRE(session.SentTo<NotifyTimeSettings>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyTimeSettings>(kOtherPeer)[0]->timeModel.Time == session.HostClock.Time);

    // Sleeping a whole day lands on the same hour: still a jump, days passed with it.
    session.Outbox.clear();
    session.HostClock.Day += 1;
    session.HostClock.DaysPassed += 1;
    session.Frame(62000);
    REQUIRE(session.SentTo<NotifyTimeSettings>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyTimeSettings>(kPeer)[0]->timeModel.DaysPassed == session.HostClock.DaysPassed);

    // The clock's speed changing.
    session.Outbox.clear();
    session.HostClock.TimeScale = 6.f;
    session.Frame(62000);
    CHECK(session.SentTo<NotifyTimeSettings>(kPeer).size() == 1);
}

TEST_CASE("Set time: any player's sets the host's clock", "[HostAuthority][Time]")
{
    Session session;
    SetTimeCommandRequest command;
    command.Hours = 21;
    command.Minutes = 30;
    session.Receive(kPeer, command);
    CHECK(session.HostClock.Time == Approx(21.5f));

    // Peers then follow it like any jump of the host's clock.
    session.Frame(1000);
    REQUIRE(session.SentTo<NotifyTimeSettings>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyTimeSettings>(kOtherPeer)[0]->timeModel.Time == Approx(21.5f));

    session.Outbox.clear();
    command.Hours = 24; // not an hour
    session.Receive(kHost, command);
    CHECK(session.HostClock.Time == Approx(21.5f));
}

TEST_CASE("Time: peers' clocks count the days passed at midnight themselves", "[HostAuthority][Time]")
{
    Session session;
    session.HostClock.TimeScale = 20.f;
    session.HostClock.Time = 23.9f;
    session.HostClock.Day = 31;
    session.HostClock.Month = 11;
    session.HostClock.Year = 201;
    session.HostClock.DaysPassed = 300;
    session.Frame(1000);
    session.Outbox.clear();

    // A real minute at 20x crosses midnight and the new year, as the engine's clock rolls them.
    session.HostClock.Time = 23.9f + 20.f / 60.f - 24.f;
    session.HostClock.Day = 1;
    session.HostClock.Month = 0;
    session.HostClock.Year = 202;
    session.HostClock.DaysPassed = 301;
    session.Frame(61000);
    CHECK(session.Outbox.empty());

    // Sleeping 30 hours.
    session.HostClock.Time += 6.f;
    session.HostClock.Day = 2;
    session.HostClock.DaysPassed = 302;
    session.Frame(61500);
    REQUIRE(session.SentTo<NotifyTimeSettings>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyTimeSettings>(kPeer)[0]->timeModel.DaysPassed == 302);
}

TEST_CASE("Travel time: only a guest's finite positive native step advances the host", "[HostAuthority][Time]")
{
    Session session;
    const float cBefore = session.HostClock.Time;
    TravelTimeRequest travel;
    travel.Hours = 3.f;
    session.Receive(kHost, travel);
    CHECK(session.HostClock.Time == cBefore);
    session.Receive(kPeer, travel);
    CHECK(session.HostClock.Time == cBefore + 3.f);
    for (const float cInvalid : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        travel.Hours = cInvalid;
        session.Receive(kPeer, travel);
        CHECK(session.HostClock.Time == cBefore + 3.f);
    }
}
