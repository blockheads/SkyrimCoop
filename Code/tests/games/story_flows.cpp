// A guest's story events (party-model.md rule 4): the host's story manager starts the party's quests, so each event a
// guest's game raises is raised in the host's game for its character (its own game's start no quest).
#include "Party.h"

#include <Components.h>
#include <SessionIds.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;    // where everyone starts
constexpr uint32_t kSinkhole = 0x0009C5E1; // another interior
constexpr uint32_t kSmokeLocation = 0x0001B1A1;
constexpr uint32_t kSinkholeLocation = 0x0001B2C2;

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.Locations = {{kSmoke, kSmokeLocation}, {kSinkhole, kSinkholeLocation}};
    aEngine.LoadCell(kSmoke);
}

bool Together(Party& aParty, uint32_t aCell)
{
    for (World* pGame : {&aParty.Host(), &aParty.Guest()})
        for (const auto& [cId, cActor] : Party::Engine(*pGame).Actors)
            if (cActor.Player && cActor.Cell != aCell)
                return false;
    return true;
}
} // namespace

SCENARIO("A guest's arrival is raised in the host's game for its character", "[story]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    party.Until([&] { return Together(party, kSmoke); }, "the party is together in QASmoke");
    host.StoriesRaised.clear();

    party.As(party.Guest(), [&] { Party::Engine(party.Guest()).PlayerTravels(kSinkhole, {5.f, 5.f, 0.f}); });
    party.Until([&] { return Together(party, kSinkhole); }, "the party is together in the sinkhole");
    party.Until([&] { return !host.StoriesRaised.empty(); }, "the host's game raises the guest's arrival");
    for (int i = 0; i < 10; ++i)
        party.Frame();

    THEN("the host's game raised it once, for the guest's character, from QASmoke's location to the sinkhole's")
    {
        using Member = StoryEvent::Member;
        REQUIRE(host.StoriesRaised.size() == 1);
        const StoryEvent& cRaised = host.StoriesRaised[0];
        CHECK(cRaised.Type == StoryEvent::TypeOf("CLOC"));
        CHECK(cRaised.Members == std::vector<Member>{{Member::kCharacter}, {Member::kForm, GameId(0, kSmokeLocation)},
                                                     {Member::kForm, GameId(0, kSinkholeLocation)}});
        party.As(party.Host(), [&] {
            const uint32_t cCopy = Utils::GetFormIdBySessionId(cRaised.Character);
            REQUIRE(host.Actors.contains(cCopy));
            CHECK(host.Actors.at(cCopy).Copy);
            CHECK(host.Actors.at(cCopy).Player);
        });
    }
    AND_THEN("the guest's game raised none: its story manager starts no quest")
    {
        CHECK(Party::Engine(party.Guest()).StoriesRaised.empty());
    }
}

SCENARIO("The host's arrival raises nothing for anyone: its engine raises its own", "[story]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    party.Until([&] { return Together(party, kSmoke); }, "the party is together in QASmoke");
    host.StoriesRaised.clear();

    party.As(party.Host(), [&] { host.PlayerTravels(kSinkhole, {5.f, 5.f, 0.f}); });
    party.Until([&] { return Together(party, kSinkhole); }, "the party is together in the sinkhole");
    for (int i = 0; i < 10; ++i)
        party.Frame();
    // The guest's game follows the host's: its arrival raised for its character too, the story manager's PartyArrivals
    // then drops it (the host's player arrived first, ClientTests).
    CHECK(host.StoriesRaised.size() <= 1);
    for (const StoryEvent& cRaised : host.StoriesRaised)
        CHECK(cRaised.Members.back() == StoryEvent::Member{StoryEvent::Member::kForm, GameId(0, kSinkholeLocation)});
}

SCENARIO("Any story event a guest's game raises is the host's, but one naming another game's actor", "[story]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return Together(party, kSmoke); }, "the party is together in QASmoke");
    host.StoriesRaised.clear();

    // A skill increase (SKIL, a skill's index): a type nothing here names, as a mod's own event would be.
    party.As(party.Guest(), [&] { guest.RaisesStoryEvent(StoryEvent::TypeOf("SKIL"), {17}); });
    party.Until([&] { return !host.StoriesRaised.empty(); }, "the host's game raises the guest's skill increase");
    CHECK(host.StoriesRaised[0].Type == StoryEvent::TypeOf("SKIL"));
    CHECK(host.StoriesRaised[0].Members == std::vector<StoryEvent::Member>{{StoryEvent::Member::kInteger, {}, 17}});

    // A kill of the host's NPC (its copy here): the host's game raises its own, with the guest's character as killer.
    constexpr uint32_t cNpc = 0x1A67C;
    party.As(party.Host(), [&] { host.LoadNpc(cNpc, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return !guest.CopiesOf(cNpc).empty(); }, "the guest sees the NPC");
    party.As(party.Guest(), [&] {
        guest.RaisesStoryEvent(StoryEvent::TypeOf("KILL"), {guest.CopiesOf(cNpc)[0], FakeEngine::kPlayer});
    });
    for (int i = 0; i < 10; ++i)
        party.Frame();
    CHECK(host.StoriesRaised.size() == 1);
}
