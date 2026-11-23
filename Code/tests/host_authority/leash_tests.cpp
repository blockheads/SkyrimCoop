// HostLeash: a guest outdoors out of the host's grid hears a countdown, then goes to the host (party-travel.md).
#include "session.h"

#include <Messages/EnterCellRequest.h>
#include <Messages/NotifyPartyTravel.h>
#include <Messages/TravelMessages.h>

namespace
{
constexpr float kCell = 4096.f;
using Kind = NotifyLeash::Kind;

// aPlayer's character announced in a worldspace (Tamriel 3C) at aPosition.
void AnnounceOutdoors(Session& aSession, ConnectionId_t aPlayer, glm::vec3 aPosition, uint32_t aWorldSpace = 0x3C)
{
    AnnounceCharacterRequest announce;
    announce.SessionId = aSession.CharacterId(aPlayer);
    announce.ReferenceId = GameId(0, 0x14);
    announce.CellId = GameId(0, 0x9732);
    announce.WorldSpaceId = GameId(0, aWorldSpace);
    announce.Position = aPosition;
    aSession.Receive(aPlayer, announce);
}

void Walk(Session& aSession, ConnectionId_t aPlayer, glm::vec3 aPosition)
{
    ReferencesMoveRequest movement;
    movement.Updates[aSession.CharacterId(aPlayer)].UpdatedMovement.Position = aPosition;
    aSession.Receive(aPlayer, movement);
}

EnterCellRequest Arrival(glm::vec3 aPosition, EnterCellRequest::Travel aHow, uint32_t aCell = 0x9732, uint32_t aWorldSpace = 0x3C)
{
    EnterCellRequest arrival;
    arrival.How = aHow;
    arrival.CellId = GameId(0, aCell);
    if (aWorldSpace)
        arrival.WorldSpaceId = GameId(0, aWorldSpace);
    arrival.Position = aPosition;
    return arrival;
}

void Falls(Session& aSession, ConnectionId_t aPlayer, bool aDown)
{
    PlayerDeathRequest request;
    request.SessionId = aSession.CharacterId(aPlayer);
    request.Generation = 1;
    request.Down = aDown;
    aSession.Receive(aPlayer, request);
}

// The frames' clock: each Frame(aMs) is that much later.
struct Clock
{
    Session& Games;
    uint64_t Now = 1000;
    void Pass(uint64_t aMs)
    {
        Now += aMs;
        Games.Frame(Now);
    }
};

// The host and kPeer in Tamriel side by side; kOtherPeer not announced unless a case does.
Clock Outdoors(Session& aSession)
{
    AnnounceOutdoors(aSession, kHost, {100.f, 100.f, 0.f});
    AnnounceOutdoors(aSession, kPeer, {200.f, 100.f, 0.f});
    aSession.Frame(1000);
    aSession.Outbox.clear();
    return {aSession};
}

// The grace before a countdown starts, and a frame to notice the player out first.
void Grace(Clock& aClock)
{
    aClock.Pass(100);
    aClock.Pass(static_cast<uint64_t>(HostLeash::kGraceSeconds * 1000));
}

std::vector<Kind> Leashes(const Session& acSession, ConnectionId_t aPlayer)
{
    std::vector<Kind> states;
    for (const auto* cpLeash : acSession.SentTo<NotifyLeash>(aPlayer))
        states.push_back(cpLeash->State);
    return states;
}
} // namespace

TEST_CASE("Leash: a guest out of the host's grid gets a countdown, and coming back in time stops it", "[HostAuthority][Leash]")
{
    Session session;
    Clock clock = Outdoors(session);

    // Two cells off: still in the host's grid.
    Walk(session, kPeer, {2 * kCell + 100.f, 100.f, 0.f});
    clock.Pass(100);
    CHECK(session.SentTo<NotifyLeash>(kPeer).empty());

    Walk(session, kPeer, {3 * kCell + 100.f, 100.f, 0.f});
    clock.Pass(100);
    clock.Pass(1800);
    CHECK(session.SentTo<NotifyLeash>(kPeer).empty()); // not yet: out under 2 s
    clock.Pass(200);
    auto leashes = session.SentTo<NotifyLeash>(kPeer);
    REQUIRE(leashes.size() == 1);
    CHECK(leashes[0]->State == Kind::kOut);
    CHECK(leashes[0]->Seconds == HostLeash::kCountdownSeconds);
    CHECK(session.SentTo<NotifyLeash>(kHost).empty());
    clock.Pass(100);
    CHECK(session.SentTo<NotifyLeash>(kPeer).size() == 1); // once, not every frame

    session.Outbox.clear();
    Walk(session, kPeer, {2 * kCell + 100.f, 100.f, 0.f});
    clock.Pass(5000);
    CHECK(Leashes(session, kPeer) == std::vector{Kind::kBack});
    clock.Pass(20000);
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());
}

TEST_CASE("Leash: the grid's edge is the third cell, on every side and across the origin", "[HostAuthority][Leash]")
{
    struct Spot
    {
        glm::vec3 Host;
        glm::vec3 Guest;
        bool Out;
    };
    const Spot cSpots[] = {
        {{100.f, 100.f, 0.f}, {2 * kCell + 4000.f, 2 * kCell + 4000.f, 0.f}, false},  // the far corner of the grid
        {{100.f, 100.f, 0.f}, {3 * kCell + 1.f, 100.f, 0.f}, true},                   // east
        {{100.f, 100.f, 0.f}, {100.f, 3 * kCell + 1.f, 0.f}, true},                   // north
        {{100.f, 100.f, 0.f}, {-2 * kCell - 1.f, 100.f, 0.f}, true},                  // west: cell -3 starts just past -2 cells
        {{100.f, 100.f, 0.f}, {-2 * kCell + 1.f, 100.f, 0.f}, false},                 // cell -2
        {{100.f, 100.f, 0.f}, {100.f, -2 * kCell - 1.f, 0.f}, true},                  // south
        {{-100.f, -100.f, 0.f}, {kCell + 4000.f, kCell + 4000.f, 0.f}, false},        // the host in cell (-1, -1): to (1, 1)
        {{-100.f, -100.f, 0.f}, {2 * kCell + 1.f, -100.f, 0.f}, true},
        {{4095.f, 100.f, 0.f}, {3 * kCell - 1.f, 100.f, 0.f}, false},                 // 8190 units apart, two cells
        {{100.f, 100.f, 0.f}, {3 * kCell + 1.f, 3 * kCell + 1.f, 50000.f}, true},     // height never counts
    };
    for (const Spot& cSpot : cSpots)
    {
        Session session;
        AnnounceOutdoors(session, kHost, cSpot.Host);
        AnnounceOutdoors(session, kPeer, cSpot.Host);
        session.Frame(1000);
        session.Outbox.clear();
        Walk(session, kPeer, cSpot.Guest);
        session.Frame(1100);
        session.Frame(3200);
        INFO("host " << cSpot.Host.x << ", " << cSpot.Host.y << "; guest " << cSpot.Guest.x << ", " << cSpot.Guest.y);
        CHECK(session.SentTo<NotifyLeash>(kPeer).size() == (cSpot.Out ? 1u : 0u));
    }
}

TEST_CASE("Leash: a guest still out when its countdown ends goes to the host, where the host is then", "[HostAuthority][Leash]")
{
    Session session;
    Clock clock = Outdoors(session);
    Walk(session, kPeer, {5 * kCell, 100.f, 0.f});
    Grace(clock);
    clock.Pass(9999);
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());
    // The host walked on meanwhile, still out of reach: the trip goes to where it stands now.
    Walk(session, kHost, {900.f, 700.f, 0.f});

    clock.Pass(1001);
    const auto cTravels = session.SentTo<NotifyPartyTravel>(kPeer);
    REQUIRE(cTravels.size() == 1);
    CHECK(cTravels[0]->WorldSpaceId == GameId(0, 0x3C));
    CHECK(cTravels[0]->Position == glm::vec3(900.f, 700.f, 0.f));
    CHECK(session.SentTo<NotifyPartyTravel>(kHost).empty());

    // On its way: no new countdown, nor a second trip, while it loads.
    session.Outbox.clear();
    clock.Pass(5000);
    CHECK(session.SentTo<NotifyLeash>(kPeer).empty());
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());

    // Arrived by the trip: nothing said; out again later counts down again.
    session.Receive(kPeer, Arrival({901.f, 700.f, 0.f}, EnterCellRequest::Travel::kTether));
    clock.Pass(100);
    CHECK(session.SentTo<NotifyLeash>(kPeer).empty());
    Walk(session, kPeer, {5 * kCell, 100.f, 0.f});
    Grace(clock);
    CHECK(Leashes(session, kPeer) == std::vector{Kind::kOut});
}

TEST_CASE("Leash: the countdown runs on frame time, and starts over after a return", "[HostAuthority][Leash]")
{
    Session session;
    Clock clock = Outdoors(session);
    Walk(session, kPeer, {5 * kCell, 100.f, 0.f});
    Grace(clock);
    for (int i = 0; i < 9; ++i)
        clock.Pass(1000); // 9 s of its countdown
    Walk(session, kPeer, {200.f, 100.f, 0.f});
    clock.Pass(100);
    Walk(session, kPeer, {5 * kCell, 100.f, 0.f});
    Grace(clock);
    // Out again: a full 10 s, not the second that was left.
    for (int i = 0; i < 9; ++i)
        clock.Pass(1000);
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());
    clock.Pass(1100);
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).size() == 1);
    CHECK(Leashes(session, kPeer) == std::vector{Kind::kOut, Kind::kBack, Kind::kOut});
}

TEST_CASE("Leash: a trip that never lands is sent again; one that lands out of the grid too", "[HostAuthority][Leash]")
{
    Session session;
    Clock clock = Outdoors(session);
    Walk(session, kPeer, {5 * kCell, 100.f, 0.f});
    Grace(clock);
    clock.Pass(10000);
    REQUIRE(session.SentTo<NotifyPartyTravel>(kPeer).size() == 1);

    // Its game never travelled (a menu, a load in the way).
    clock.Pass(14000);
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).size() == 1);
    clock.Pass(1100);
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).size() == 2);

    // It arrives where the host was; the host walked 6 cells on: no new countdown, sent on after the wait.
    session.Outbox.clear();
    Walk(session, kHost, {-6 * kCell, 100.f, 0.f});
    session.Receive(kPeer, Arrival({101.f, 100.f, 0.f}, EnterCellRequest::Travel::kTether));
    clock.Pass(100);
    CHECK(session.SentTo<NotifyLeash>(kPeer).empty());
    clock.Pass(15000);
    const auto cTravels = session.SentTo<NotifyPartyTravel>(kPeer);
    REQUIRE(cTravels.size() == 1);
    CHECK(cTravels[0]->Position == glm::vec3(-6 * kCell, 100.f, 0.f));
}

TEST_CASE("Leash: the host walking away starts a guest's countdown, and walking back stops it", "[HostAuthority][Leash]")
{
    Session session;
    Clock clock = Outdoors(session);
    Walk(session, kHost, {-4 * kCell, 100.f, 0.f});
    Grace(clock);
    Walk(session, kHost, {-kCell, 100.f, 0.f});
    clock.Pass(1000);
    CHECK(Leashes(session, kPeer) == std::vector{Kind::kOut, Kind::kBack});
    clock.Pass(20000);
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());
}

TEST_CASE("Leash: each guest has its own countdown", "[HostAuthority][Leash]")
{
    Session session;
    Clock clock = Outdoors(session);
    AnnounceOutdoors(session, kOtherPeer, {300.f, 100.f, 0.f});
    session.Outbox.clear();

    Walk(session, kPeer, {5 * kCell, 100.f, 0.f});
    Grace(clock);
    clock.Pass(6000);
    Walk(session, kOtherPeer, {100.f, 5 * kCell, 0.f});
    Grace(clock);
    CHECK(Leashes(session, kPeer) == std::vector{Kind::kOut});
    CHECK(Leashes(session, kOtherPeer) == std::vector{Kind::kOut});

    clock.Pass(2000);
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyPartyTravel>(kOtherPeer).empty());
    // The two guests side by side, both far from the host: each is leashed to the host, not to the other.
    Walk(session, kOtherPeer, {100.f, 2 * kCell, 0.f});
    clock.Pass(100);
    CHECK(Leashes(session, kOtherPeer) == std::vector{Kind::kOut, Kind::kBack});
}

TEST_CASE("Leash: none indoors, in another worldspace, or before a player's character is in the world", "[HostAuthority][Leash]")
{
    Session session;
    Clock clock = Outdoors(session);

    SECTION("a guest indoors")
    {
        session.Receive(kPeer, Arrival({50 * kCell, 0.f, 0.f}, EnterCellRequest::Travel::kTether, 0x133C9, 0));
        clock.Pass(20000);
        CHECK(session.SentTo<NotifyLeash>(kPeer).empty());
    }
    SECTION("a guest in another worldspace")
    {
        session.Receive(kPeer, Arrival({50 * kCell, 0.f, 0.f}, EnterCellRequest::Travel::kTether, 0x1A26F, 0x1A26F));
        clock.Pass(20000);
        CHECK(session.SentTo<NotifyLeash>(kPeer).empty());
    }
    SECTION("a guest not announced yet")
    {
        clock.Pass(20000);
        CHECK(session.SentTo<NotifyLeash>(kOtherPeer).empty());
    }
}

TEST_CASE("Leash: a running countdown stops, unsaid, when a trip takes over", "[HostAuthority][Leash]")
{
    Session session;
    Clock clock = Outdoors(session);
    Walk(session, kPeer, {5 * kCell, 100.f, 0.f});
    Grace(clock);
    clock.Pass(3000);
    REQUIRE(Leashes(session, kPeer) == std::vector{Kind::kOut});
    session.Outbox.clear();

    SECTION("the host goes through a load door: the party tether brings the guest")
    {
        session.Receive(kHost, Arrival({0.f, 0.f, 0.f}, EnterCellRequest::Travel::kOwn, 0x133C9, 0));
        clock.Pass(100);
        CHECK(Leashes(session, kPeer) == std::vector{Kind::kOff});
        CHECK(session.SentTo<NotifyPartyTravel>(kPeer).size() == 1); // the tether's, to the interior
        clock.Pass(20000);
        CHECK(session.SentTo<NotifyPartyTravel>(kPeer).size() == 1);
    }
    SECTION("the guest goes through a load door itself")
    {
        session.Receive(kPeer, Arrival({0.f, 0.f, 0.f}, EnterCellRequest::Travel::kOwn, 0x133C9, 0));
        clock.Pass(100);
        CHECK(Leashes(session, kPeer) == std::vector{Kind::kOff});
        clock.Pass(20000);
        CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());
    }
    SECTION("the guest announces a trip of its own (fast travel) before its load")
    {
        PartyTravelRequest trip;
        trip.Destination.CellId = GameId(0, 0x9740);
        trip.Destination.WorldSpaceId = GameId(0, 0x3C);
        trip.Destination.Position = glm::vec3(9 * kCell, 0.f, 0.f);
        session.Receive(kPeer, trip);
        clock.Pass(100);
        CHECK(Leashes(session, kPeer) == std::vector{Kind::kOff});
        clock.Pass(20000);
        CHECK(session.SentTo<NotifyLeash>(kPeer).size() == 1);
    }
}

TEST_CASE("Leash: a downed player is its respawn's, not the leash's", "[HostAuthority][Leash]")
{
    Session session;
    Clock clock = Outdoors(session);

    SECTION("a guest that falls while out")
    {
        Walk(session, kPeer, {5 * kCell, 100.f, 0.f});
        Grace(clock);
        clock.Pass(3000);
        Falls(session, kPeer, true);
        clock.Pass(100);
        CHECK(Leashes(session, kPeer) == std::vector{Kind::kOut, Kind::kOff});
        clock.Pass(20000);
        CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());
    }
    SECTION("a guest already down walks nowhere")
    {
        Falls(session, kPeer, true);
        Walk(session, kPeer, {5 * kCell, 100.f, 0.f});
        clock.Pass(20000);
        CHECK(session.SentTo<NotifyLeash>(kPeer).empty());
    }
    SECTION("the host down: nobody is sent to a body")
    {
        Falls(session, kHost, true);
        Walk(session, kPeer, {5 * kCell, 100.f, 0.f});
        clock.Pass(20000);
        CHECK(session.SentTo<NotifyLeash>(kPeer).empty());
    }
}

TEST_CASE("Leash: a guest leaving mid-countdown takes its countdown along", "[HostAuthority][Leash]")
{
    Session session;
    Clock clock = Outdoors(session);
    Walk(session, kPeer, {5 * kCell, 100.f, 0.f});
    Grace(clock);
    clock.Pass(3000);
    session.Leave(kPeer);
    session.Outbox.clear();
    clock.Pass(20000);
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());
    CHECK(session.SentTo<NotifyLeash>(kPeer).empty());
}

TEST_CASE("Leash: stepping over the grid's edge and back within the grace says nothing", "[HostAuthority][Leash]")
{
    Session session;
    Clock clock = Outdoors(session);
    // Along the edge: out for a second and a half, in again, eight times.
    for (int i = 0; i < 8; ++i)
    {
        Walk(session, kPeer, {3 * kCell + 10.f, 100.f, 0.f});
        clock.Pass(100);
        clock.Pass(1500);
        Walk(session, kPeer, {3 * kCell - 10.f, 100.f, 0.f});
        clock.Pass(500);
    }
    CHECK(session.SentTo<NotifyLeash>(kPeer).empty());
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());

    // Each time out starts the grace over: 1.5 s out, a moment in, 1.5 s out is not 2 s out.
    Walk(session, kPeer, {3 * kCell + 10.f, 100.f, 0.f});
    clock.Pass(100);
    clock.Pass(1500);
    Walk(session, kPeer, {3 * kCell - 10.f, 100.f, 0.f});
    clock.Pass(16);
    Walk(session, kPeer, {3 * kCell + 10.f, 100.f, 0.f});
    clock.Pass(16);
    clock.Pass(1500);
    CHECK(session.SentTo<NotifyLeash>(kPeer).empty());
    // Staying out: the countdown after the grace, and the trip 10 s after that.
    clock.Pass(600);
    CHECK(Leashes(session, kPeer) == std::vector{Kind::kOut});
    clock.Pass(9900);
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());
    clock.Pass(200);
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).size() == 1);
}
