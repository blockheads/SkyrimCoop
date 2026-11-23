// HostWeather: the session's weather is the host's, per region (docs/re/systems/weather.md).
#include "session.h"

namespace
{
const GameId kClear(0, 0x81A);
const GameId kSnow(0, 0xC8221);
const GameId kStorm(0, 0x10A241);
const GameId kWinterhold(0, 0x10FDC6);
const GameId kRiverwood(0, 0x2A72D);

WeatherRegionRequest InRegion(const GameId& acRegion = {})
{
    WeatherRegionRequest request;
    request.Region = acRegion;
    return request;
}

// Each peer's first report (a guest's on joining): no region.
void Reported(Session& aSession)
{
    aSession.Receive(kPeer, InRegion());
    aSession.Receive(kOtherPeer, InRegion());
}
} // namespace

TEST_CASE("Weather: peers get the host's weather when it changes", "[HostAuthority][Weather]")
{
    Session session;
    Reported(session);

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

TEST_CASE("Weather: a joining player gets the host's weather", "[HostAuthority][Weather]")
{
    Session session;
    session.Weather = GameId(0, 0x81A);
    session.Frame(1000);
    session.Receive(kPeer, InRegion());
    session.Outbox.clear();

    AnnounceCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    announce.SessionId = session.CharacterId(kPeer);
    session.Receive(kPeer, announce);
    REQUIRE(session.SentTo<NotifyWeatherChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyWeatherChange>(kPeer)[0]->Id == GameId(0, 0x81A));
}


TEST_CASE("Weather: a guest gets the weather the host's game gives its region", "[HostAuthority][Weather]")
{
    Session session;
    session.Weather = kSnow;
    session.WeatherRegion = kWinterhold;
    session.RegionWeathers = {{kWinterhold.BaseId, kSnow}, {kRiverwood.BaseId, kClear}};
    session.Frame(1000);
    // Nothing before a peer's first report: a fade toward the wrong region's weather would run first.
    CHECK(session.SentTo<NotifyWeatherChange>(kPeer).empty());
    Reported(session);
    REQUIRE(session.SentTo<NotifyWeatherChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyWeatherChange>(kPeer)[0]->Id == kSnow);

    // The peer walks into Riverwood, whose list has no snow: the host's game holds clear there.
    session.Outbox.clear();
    session.Receive(kPeer, InRegion(kRiverwood));
    REQUIRE(session.SentTo<NotifyWeatherChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyWeatherChange>(kPeer)[0]->Id == kClear);
    CHECK(session.SentTo<NotifyWeatherChange>(kOtherPeer).empty());

    // In the host's region, the host's own; the same region again sends nothing.
    session.Outbox.clear();
    session.Receive(kPeer, InRegion(kWinterhold));
    REQUIRE(session.SentTo<NotifyWeatherChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyWeatherChange>(kPeer)[0]->Id == kSnow);
    session.Outbox.clear();
    session.Receive(kPeer, InRegion(kWinterhold));
    CHECK(session.Outbox.empty());
}

TEST_CASE("Weather: forced weather wins in every region", "[HostAuthority][Weather]")
{
    Session session;
    session.Weather = kSnow;
    session.WeatherRegion = kWinterhold;
    session.RegionWeathers = {{kWinterhold.BaseId, kSnow}, {kRiverwood.BaseId, kClear}};
    session.Frame(1000);
    session.Receive(kPeer, InRegion(kRiverwood));
    session.Outbox.clear();

    session.Weather = kStorm;
    session.WeatherForced = true;
    session.Frame(1100);
    REQUIRE(session.SentTo<NotifyWeatherChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyWeatherChange>(kPeer)[0]->Id == kStorm);

    // Released: the region's own again.
    session.Outbox.clear();
    session.Weather = kSnow;
    session.WeatherForced = false;
    session.Frame(1200);
    REQUIRE(session.SentTo<NotifyWeatherChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyWeatherChange>(kPeer)[0]->Id == kClear);
}

TEST_CASE("Weather: no region, or one without weathers, gets the host's", "[HostAuthority][Weather]")
{
    Session session;
    session.Weather = kSnow;
    session.WeatherRegion = kWinterhold;
    session.Frame(1000);
    Reported(session);
    session.Outbox.clear();

    // A region the host's game holds no weather for (none in its list): the host's.
    session.Receive(kPeer, InRegion(kRiverwood));
    CHECK(session.Outbox.empty());
    // An interior without a sky: none, the host's.
    session.Receive(kPeer, InRegion(GameId{}));
    CHECK(session.Outbox.empty());
}

TEST_CASE("Weather: a region's own weather changing reaches a guest there", "[HostAuthority][Weather]")
{
    Session session;
    session.Weather = kSnow;
    session.WeatherRegion = kWinterhold;
    session.RegionWeathers = {{kWinterhold.BaseId, kSnow}, {kRiverwood.BaseId, kClear}};
    session.Frame(1000);
    session.Receive(kPeer, InRegion(kRiverwood));
    session.Outbox.clear();

    // A load rolls every region again (26231), the host's shown weather unchanged.
    session.RegionWeathers[kRiverwood.BaseId] = kStorm;
    session.Frame(1100);
    REQUIRE(session.SentTo<NotifyWeatherChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyWeatherChange>(kPeer)[0]->Id == kStorm);
    CHECK(session.SentTo<NotifyWeatherChange>(kOtherPeer).empty());
}
