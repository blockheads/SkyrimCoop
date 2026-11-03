#include "Party.h"
#include "Fixtures.h"
#include "FakeMapMovie.h"
#include <Interface/PartyMarkerLabel.h>
#include <Services/PartyMarkers.h>
#include <Services/SessionService.h>
#include <algorithm>

namespace
{
const PartyMarker* MarkerFor(const FakeEngine& acEngine, uint32_t aPlayer)
{
    const auto& cMembers = acEngine.PartyMarkers.Members;
    const auto cIt = std::find_if(cMembers.begin(), cMembers.end(), [=](const auto& acMarker) { return acMarker.PlayerId == aPlayer; });
    return cIt == cMembers.end() ? nullptr : &*cIt;
}
}

SCENARIO("Party markers name every other player and follow the displayed copies", "[party-markers][shared]")
{
    Party party(2, SmokeWorld);
    party.Until([&] {
        return Party::Engine(party.Host()).PartyMarkers.Members.size() == 2 &&
            Party::Engine(party.Guest()).PartyMarkers.Members.size() == 2 &&
            Party::Engine(party.Guest(1)).PartyMarkers.Members.size() == 2;
    }, "every game can find both other players");
    for (World* pGame : {&party.Host(), &party.Guest(), &party.Guest(1)})
    {
        auto& engine = Party::Engine(*pGame);
        std::set<uint32_t> ids;
        for (const auto& cMarker : engine.PartyMarkers.Members)
        {
            CHECK(cMarker.PlayerId != pGame->GetTransport().GetLocalPlayerId());
            CHECK(cMarker.FormId != FakeEngine::kPlayer);
            CHECK(ids.insert(cMarker.PlayerId).second);
            CHECK(cMarker.Name == pGame->GetSessionService().NameOf(cMarker.PlayerId));
            const auto& cActor = engine.Actors.at(cMarker.FormId);
            CHECK(cMarker.CellId == cActor.Cell);
            CHECK(cMarker.Position.x == cActor.Position[0]);
        }
    }
    const uint32_t cGuest = party.Guest().GetTransport().GetLocalPlayerId();
    party.As(party.Guest(), [&] { Party::Engine(party.Guest()).Actors.at(FakeEngine::kPlayer).Position[0] += 120.f; });
    const float cDestination = Party::Engine(party.Guest()).Actors.at(FakeEngine::kPlayer).Position[0];
    party.Until([&] {
        const auto* cpMarker = MarkerFor(Party::Engine(party.Host()), cGuest);
        return cpMarker && std::abs(cpMarker->Position.x - cDestination) < 1.f;
    }, "the guest's moving marker follows its character");
}

SCENARIO("Party markers disappear on departure and return once on rejoin", "[party-markers][session]")
{
    Party party(2, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& other = Party::Engine(party.Guest(1));
    party.Until([&] { return host.PartyMarkers.Members.size() == 2; }, "the host sees both players");
    const uint32_t cId = party.Guest().GetTransport().GetLocalPlayerId();
    party.Leave(party.Guest());
    party.Until([&] {
        return !MarkerFor(host, cId) && !MarkerFor(other, cId) && guest.PartyMarkers.Members.empty();
    }, "the departed player's markers are removed on every game");
    party.Join(party.Guest());
    party.Until([&] { return host.PartyMarkers.Members.size() == 2 && guest.PartyMarkers.Members.size() == 2; },
                "rejoining creates one marker per other player");
}

SCENARIO("Finding party members leaves the shared destination waypoint alone", "[party-markers][shared]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.PlacesMarker({20.f, 30.f, 0.f}, 0x3C); });
    party.Until([&] { return guest.Waypoint && !host.PartyMarkers.Members.empty(); }, "both kinds of marker coexist");
    party.Leave(party.Guest());
    party.Until([&] { return host.PartyMarkers.Members.empty(); }, "the guest's location is removed");
    REQUIRE(host.Waypoint);
    CHECK(host.Waypoint->Position.x == 20.f);
    CHECK(host.Waypoint->WorldSpace == 0x3C);
}

SCENARIO("Party names remain visible without hovering a native map pin", "[party-markers][shared]")
{
    Party party(1, SmokeWorld);
    party.Until([&] {
        return !Party::Engine(party.Host()).PartyMarkers.Members.empty() &&
            !Party::Engine(party.Guest()).PartyMarkers.Members.empty();
    }, "each game has the other player's marker");
    for (World* cpGame : {&party.Host(), &party.Guest()})
    {
        const auto& cMarker = Party::Engine(*cpGame).PartyMarkers.Members.front();
        for (const std::string cMap : {"_root.WorldMap", "_root.WorldMap.LocalMapMenu.IconDisplay"})
        {
            FakeMapMovie movie;
            const std::string cPath = cMap + ".MarkerContainer.SkyrimCoopPartyMarkers.Player" + std::to_string(cMarker.PlayerId);
            movie.AttachMarker(cPath);
            CHECK_FALSE(movie.NameVisible(cPath));
            SetPartyMarkerLabel(movie, cPath, cMarker.Name);
            CHECK(std::get<std::string>(movie.Variables.at(cPath + ".textField.text")) == cMarker.Name);
            CHECK(movie.NameVisible(cPath));
        }
    }
}
