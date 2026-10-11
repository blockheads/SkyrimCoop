// HostWeather: the session's weather is the host's.
#include "session.h"

TEST_CASE("Weather: peers get the host's weather when it changes", "[Bridge][Weather]")
{
    Session session;

    // No sky (or the map's): nothing to share.
    session.Frame(1000);
    CHECK(session.SentTo<NotifyWeatherChange>(kPeer).empty());

    session.Weather = GameId(0, 0x81A);
    session.Frame(1100);
    REQUIRE(session.SentTo<NotifyWeatherChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyWeatherChange>(kOtherPeer)[0]->Id == GameId(0, 0x81A));
    CHECK(session.SentTo<NotifyWeatherChange>(kHost).empty());

    // The same weather again, or none for a while (an interior): peers keep what they hold.
    session.Outbox.clear();
    session.Frame(1200);
    session.Weather.reset();
    session.Frame(1300);
    session.Weather = GameId(0, 0x81A);
    session.Frame(1400);
    CHECK(session.Outbox.empty());

    session.Weather = GameId(0, 0x10A23);
    session.Frame(1500);
    REQUIRE(session.SentTo<NotifyWeatherChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyWeatherChange>(kPeer)[0]->Id == GameId(0, 0x10A23));
}

TEST_CASE("Weather: a joining player gets the host's weather", "[Bridge][Weather]")
{
    Session session;
    session.Weather = GameId(0, 0x81A);
    session.Frame(1000);
    session.Outbox.clear();

    AssignCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    session.Receive(kPeer, announce);
    REQUIRE(session.SentTo<NotifyWeatherChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyWeatherChange>(kPeer)[0]->Id == GameId(0, 0x81A));
}
