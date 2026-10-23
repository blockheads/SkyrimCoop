// The party tether across games (docs/design/host-authority-migration.md, "Party tether"; copy-contract.md):
// a player who travels by itself leads, everyone else follows; the players' copies travel with the party, the
// NPCs' stay with their cells.
#include "Party.h"

#include <Components.h>

#include <format>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;    // where everyone starts
constexpr uint32_t kSinkhole = 0x0009C5E1; // another interior
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint32_t kVilkas = 0x0001A695; // an NPC placed in kSmoke (the same reference in every game)

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

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
    Party party(1, Plugins);
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
    Party party(2, Plugins);
    party.Until([&] { return Together(party, 3, kSmoke); }, "the party is together in QASmoke");

    party.As(party.Guest(0), [&] { Party::Engine(party.Guest(0)).PlayerTravels(kSinkhole, {5.f, 5.f, 0.f}); });
    party.Until([&] { return Together(party, 3, kSinkhole); }, "the party is together in the sinkhole",
                [&] { return Why(party, 3); });
    CheckTogether(party, 3);
    party.Agree("the three games agree on the players");
}

SCENARIO("An NPC stays with its cell when the party leaves it", "[travel]")
{
    Party party(1, Plugins);
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
        Plugins(aEngine);
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
    Party party(1, Plugins);
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
