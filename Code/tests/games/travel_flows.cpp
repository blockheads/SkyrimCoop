// The party tether across games (docs/design/host-authority-migration.md, "Party tether"; copy-contract.md):
// a player who travels by itself leads, everyone else follows; the players' copies travel with the party, the
// NPCs' stay with their cells.
#include "Party.h"
#include "Fixtures.h"
#include "FakeDialogue.h"

#include <Components.h>
#include <Events/FastTravelEvent.h>
#include <Events/PlayerTravelQueuedEvent.h>

#include <format>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;    // where everyone starts
constexpr uint32_t kSinkhole = 0x0009C5E1; // another interior
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint32_t kVilkas = 0x0001A695; // an NPC placed in kSmoke (the same reference in every game)

// The cells each game sees the party in: its own player's, and each player copy's.
std::vector<std::string> PartyApart(Party& aParty, size_t aGames)
{
    std::vector<std::string> apart;
    for (size_t i = 0; i < aGames; ++i)
    {
        const FakeEngine& cEngine = Party::Engine(i == 0 ? aParty.Host() : aParty.Guest(i - 1));
        const uint32_t cCell = cEngine.Actors.at(FakeEngine::kPlayer).Cell;
        size_t players = 0;
        for (const auto& [cId, cActor] : cEngine.Actors)
        {
            if (!cActor.Player || cId == FakeEngine::kPlayer)
                continue;
            ++players;
            if (cActor.Cell != cCell)
                apart.push_back(std::format("{}: a player's copy {:X} is in cell {:X}, its player in {:X}",
                                            cEngine.Name, cId, cActor.Cell, cCell));
        }
        if (players != aGames - 1)
            apart.push_back(std::format("{}: {} players' copies, not {}", cEngine.Name, players, aGames - 1));
    }
    return apart;
}

std::string Why(Party& aParty, size_t aGames)
{
    std::string why;
    for (size_t i = 0; i < aGames; ++i)
    {
        const FakeEngine& cEngine = Party::Engine(i == 0 ? aParty.Host() : aParty.Guest(i - 1));
        why += std::format("{} in {:X}; ", cEngine.Name, cEngine.Actors.at(FakeEngine::kPlayer).Cell);
    }
    for (const auto& cApart : PartyApart(aParty, aGames))
        why += cApart + "; ";
    return why;
}

bool Together(Party& aParty, size_t aGames, uint32_t aCell)
{
    for (size_t i = 0; i < aGames; ++i)
        if (Party::Engine(i == 0 ? aParty.Host() : aParty.Guest(i - 1)).Actors.at(FakeEngine::kPlayer).Cell != aCell)
            return false;
    return PartyApart(aParty, aGames).empty();
}

void CheckTogether(Party& aParty, size_t aGames)
{
    for (const auto& cApart : PartyApart(aParty, aGames))
        UNSCOPED_INFO(cApart);
    CHECK(PartyApart(aParty, aGames).empty());
}
} // namespace

SCENARIO("The host travels: the guest follows, and the players' copies come along", "[travel]")
{
    Party party(1, SmokeWorld);
    party.Until([&] { return Together(party, 2, kSmoke); }, "the party is together in QASmoke");

    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlayerTravels(kSinkhole, {100.f, 200.f, 0.f}); });
    party.Until([&] { return Together(party, 2, kSinkhole); }, "the party is together in the sinkhole",
                [&] { return Why(party, 2); });
    THEN("the guest stands where the host arrived")
    {
        const auto& cGuest = Party::Engine(party.Guest()).Actors.at(FakeEngine::kPlayer);
        CHECK(cGuest.Position[0] == 100.f);
        CHECK(cGuest.Position[1] == 200.f);
    }
    CheckTogether(party, 2);
    party.Agree("the games agree on the players");
}

SCENARIO("A guest travels: the host and the other guest follow", "[travel]")
{
    Party party(2, SmokeWorld);
    party.Until([&] { return Together(party, 3, kSmoke); }, "the party is together in QASmoke");

    party.As(party.Guest(0), [&] { Party::Engine(party.Guest(0)).PlayerTravels(kSinkhole, {5.f, 5.f, 0.f}); });
    party.Until([&] { return Together(party, 3, kSinkhole); }, "the party is together in the sinkhole",
                [&] { return Why(party, 3); });
    CheckTogether(party, 3);
    party.Agree("the three games agree on the players");
}

SCENARIO("An NPC stays with its cell when the party leaves it", "[travel]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSmoke); });
    party.Until([&] { return !guest.CopiesOf(kGuard).empty(); }, "the guest has the host's guard");

    WHEN("the host leads the party away")
    {
        party.As(party.Host(), [&] { host.PlayerTravels(kSinkhole, {}); });
        party.Until([&] { return Together(party, 2, kSinkhole); }, "the party is together in the sinkhole",
                    [&] { return Why(party, 2); });
        party.Until([&] { return guest.CopiesOf(kGuard).empty(); }, "the guest's copy of the guard goes");
        party.Agree("the games agree on the actors in the sinkhole");

        AND_WHEN("an NPC loads where the party is now")
        {
            party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSinkhole); });
            party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest gets a copy of it");
            THEN("the copy is in the sinkhole")
            {
                CHECK(guest.Actors.at(guest.CopiesOf(kGuard)[0]).Cell == kSinkhole);
                party.Agree("the games agree on the new guard");
            }
        }
    }
}

SCENARIO("A placed NPC stays with its cell when the party leaves it, and is the host's copy again on its return",
         "[travel]")
{
    Party party(1, [](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        aEngine.PlaceNpc(kVilkas, 0x0001A694, kSmoke, {100.f, 0.f, 0.f});
    });
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSmoke, {10.f, 0.f, 0.f}); });
    party.Until([&] { return guest.IsActorCopy(kVilkas) && guest.CopiesOf(kGuard).size() == 1; },
                "the guest's Vilkas is the host's copy, and it has the host's guard");
    party.Agree("the games agree on Vilkas and the guard");

    WHEN("the host leads the party away")
    {
        party.As(party.Host(), [&] { host.PlayerTravels(kSinkhole, {}); });
        party.Until([&] { return Together(party, 2, kSinkhole); }, "the party is together in the sinkhole",
                    [&] { return Why(party, 2); });
        party.Until([&] { return guest.CopiesOf(kGuard).empty(); }, "the guest's copy of the guard goes");
        THEN("the guest's Vilkas stays in QASmoke, held as a copy")
        {
            CHECK(guest.Actors.at(kVilkas).Cell == kSmoke);
            CHECK(guest.IsActorCopy(kVilkas));
            party.Agree("the games agree on the actors");
        }

        AND_WHEN("the host leads it back")
        {
            party.As(party.Host(), [&] { host.PlayerTravels(kSmoke, {}); });
            party.Until([&] { return Together(party, 2, kSmoke); }, "the party is back in QASmoke",
                        [&] { return Why(party, 2); });
            party.Until(
                [&] {
                    for (const auto cEntity : party.Guest().view<FormIdComponent, RemoteComponent>())
                        if (party.Guest().get<FormIdComponent>(cEntity).Id == kVilkas)
                            return true;
                    return false;
                },
                "the guest's Vilkas is the host's copy again");
            party.Agree("the games agree on Vilkas back");
        }
    }

    WHEN("the guest leads the party away: its arrival drops its copies before the host's game leaves")
    {
        const size_t cGuards = guest.Spawned[kGuard];
        party.As(party.Guest(), [&] { guest.PlayerTravels(kSinkhole, {}); });
        party.Until([&] { return Together(party, 2, kSinkhole); }, "the party is together in the sinkhole",
                    [&] { return Why(party, 2); });
        THEN("the guest's Vilkas stays in QASmoke, held as a copy, and nothing the host left is sent it")
        {
            party.Agree("the games agree on the actors");
            CHECK(guest.Actors.at(kVilkas).Cell == kSmoke);
            CHECK(guest.IsActorCopy(kVilkas));
            CHECK(guest.Spawned[kGuard] == cGuards);
        }
    }
}

SCENARIO("An NPC the host's game loads as a following guest's arrival reaches it: the guest gets its copy", "[travel]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Agree("the games agree on the players");

    // The host leads; the guest follows and its arrival is on its way to the host (sent on its next frame).
    party.As(party.Host(), [&] { host.PlayerTravels(kSinkhole, {}); });
    party.Step(party.Host());
    party.Until([&] { return guest.GetPlayerParentCellId() == kSinkhole; }, "the guest follows to the sinkhole", {},
                &party.Guest());
    party.Step(party.Guest());

    WHEN("in the host's next frame its game announces an NPC, then its rules take the guest's arrival")
    {
        // The host's game owns the guard from its announcement; its rules take the guest's arrival after it.
        party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSinkhole, {20.f, 0.f, 0.f}); });
        party.Step(party.Host());
        party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest gets a copy of the guard");
        party.Agree("the games agree on the guard");
    }
}

SCENARIO("A guest's native fast travel advances the party calendar once across midnight", "[travel][world]")
{
    Party party(2, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    host.Calendar.Time = 23.f;
    host.Calendar.DaysPassed = 230;
    host.Calendar.Day = 17;
    party.Until([&] { return Party::Engine(party.Guest()).Calendar.DaysPassed == 230; }, "the guests have the host's date");
    party.As(party.Guest(), [&] {
        party.Guest().GetRunner().Trigger(FastTravelEvent{3.f});
        Party::Engine(party.Guest()).PlayerTravels(kSinkhole, {});
    });
    party.Until([&] { return host.Calendar.DaysPassed == 231; }, "the guest's three-hour trip advances the host across midnight");
    CHECK(host.Calendar.Time >= 2.f);
    CHECK(host.Calendar.Time < 2.05f);
    CHECK(host.Calendar.Day == 18);
    party.Until([&] { return Together(party, 3, kSinkhole) &&
                           Party::Engine(party.Guest()).Calendar.DaysPassed == 231 &&
                           Party::Engine(party.Guest(1)).Calendar.DaysPassed == 231; }, "the party and its calendar arrive together");
    party.Agree("the three games agree after fast travel");
}

SCENARIO("A queued guest travel starts the other games before its own arrival", "[travel]")
{
    Party party(2, SmokeWorld);
    party.Until([&] { return Together(party, 3, kSmoke); }, "the party starts together");
    const auto cTravels = Party::Engine(party.Guest()).Travels;
    party.As(party.Guest(), [&] {
        party.Guest().GetRunner().Trigger(PlayerTravelQueuedEvent{0, kSinkhole, {30.f, 40.f, 0.f}, 1.f});
    });
    party.Until([&] { return Party::Engine(party.Host()).GetPlayerParentCellId() == kSinkhole &&
                           Party::Engine(party.Guest(1)).GetPlayerParentCellId() == kSinkhole; },
                "the host and the other guest begin their moves before the leader arrives");
    CHECK(Party::Engine(party.Guest()).Travels == cTravels);
    CHECK(Party::Engine(party.Guest()).GetPlayerParentCellId() == kSmoke);
    party.As(party.Guest(), [&] { Party::Engine(party.Guest()).PlayerTravels(kSinkhole, {30.f, 40.f, 0.f}); });
    party.Until([&] { return Together(party, 3, kSinkhole); }, "the party converges on the actual arrival");
    party.Agree("the copies converge too");
}

SCENARIO("A dead following player travels with the party and stays dead", "[travel][death]")
{
    Party party(2, SmokeWorld);
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return Together(party, 3, kSmoke); }, "the three players start together");
    party.Until([&] {
        for (World* pGame : {&party.Host(), &party.Guest(), &party.Guest(1)})
            for (const auto cEntity : pGame->view<RemoteComponent, PlayerComponent, FormIdComponent>())
                if (pGame->any_of<WaitingFor3D, SpawnGraphStateComponent>(cEntity) ||
                    !Party::Engine(*pGame).ActorHas3D(pGame->get<FormIdComponent>(cEntity).Id))
                    return false;
        return true;
    }, "the initial player copies finish loading before the trip");
    party.As(party.Host(), [&] {
        auto& host = Party::Engine(party.Host());
        const uint32_t cEnemy = host.LoadNpc(kGuard, kSmoke);
        host.Fights(FakeEngine::kPlayer, cEnemy);
    });
    party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
    REQUIRE(guest.IsActorDead(FakeEngine::kPlayer));
    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlayerTravels(kSinkhole, {}); });
    party.Until([&] { return Together(party, 3, kSinkhole); }, "the dead guest follows the living party", [&] { return Why(party, 3); });
    CHECK(guest.IsActorDead(FakeEngine::kPlayer));
    CHECK_FALSE(guest.Reloaded);
    party.Agree("the copies agree on the travelling dead player");
}

SCENARIO("Party travel closes a following guest's dialogue and takes both guests along", "[travel][dialogue]")
{
    Party party(2, SmokeWorld);
    party.Until([&] { return Together(party, 3, kSmoke); }, "the three players start together");
    uint32_t speaker = 0;
    party.As(party.Host(), [&] { speaker = Party::Engine(party.Host()).LoadNpc(kGuard, kSmoke); });
    party.Until([&] { return !Party::Engine(party.Guest()).CopiesOf(kGuard).empty(); }, "the guest can talk to the guard");
    auto& dialogue = party.Guest().ctx().at<FakeDialogue&>();
    party.As(party.Guest(), [&] { dialogue.PlayerTalksTo(Party::Engine(party.Guest()).CopiesOf(kGuard)[0]); });
    party.Until([&] { return dialogue.MenuOpen; }, "the guest's dialogue opens");
    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlayerTravels(kSinkhole, {}); });
    party.Until([&] { return Together(party, 3, kSinkhole) && !dialogue.MenuOpen; }, "the dialogue ends as its player follows", [&] { return Why(party, 3) + (dialogue.MenuOpen ? "menu still open" : "menu closed"); });
    party.Agree("the three games agree after leaving the dialogue");
}
