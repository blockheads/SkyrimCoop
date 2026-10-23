// Copies across games (docs/design/copy-contract.md): an NPC the host's game loads gets a copy on each guest's game;
// a guest's player gets a copy on the host's; each goes when its owner's actor does.
#include "Party.h"

#include <Components.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E; // the cell everyone stands in
constexpr uint32_t kGuard = 0x0001A67C; // an NPC's base (a guard)

// That game simulates the actor and owns it in the session (CharacterService's LocalComponent).
bool OwnsHere(World& aGame, uint32_t aFormId)
{
    for (const auto cEntity : aGame.view<FormIdComponent, LocalComponent>())
        if (aGame.get<FormIdComponent>(cEntity).Id == aFormId)
            return true;
    return false;
}

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}
} // namespace

SCENARIO("An NPC the host's game loads gets a copy on the guest's game", "[copies]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t npc = 0;
    party.As(party.Host(), [&] { npc = host.LoadNpc(kGuard, kSmoke); });
    party.Until([&] { return !guest.CopiesOf(kGuard).empty(); }, "the guest's game makes a copy of the host's guard");
    THEN("one copy, in the cell, alive, and the host's guard is no copy")
    {
        CHECK(guest.CopiesOf(kGuard).size() == 1);
        CHECK(guest.Actors.at(guest.CopiesOf(kGuard)[0]).Cell == kSmoke);
        CHECK(!host.Actors.at(npc).Copy);
    }

    WHEN("the host's guard goes (its cell unloads it) before the host's game has its assignment back")
    {
        // The host's rules sent the spawn with the assignment: the guest has its copy a frame before the host's
        // game knows it owns the guard (its answer is handled on its next update).
        REQUIRE(!OwnsHere(party.Host(), npc));
        party.As(party.Host(), [&] { host.Actors.erase(npc); });
        party.Until([&] { return guest.CopiesOf(kGuard).empty(); }, "the guest's copy goes");
    }

    WHEN("the host's guard goes once the host's game owns it")
    {
        party.Until([&] { return OwnsHere(party.Host(), npc); }, "the host's game owns its guard");
        party.As(party.Host(), [&] { host.Actors.erase(npc); });
        party.Until([&] { return guest.CopiesOf(kGuard).empty(); }, "the guest's copy goes");
    }
}

SCENARIO("A guest's player gets a copy on the host's game", "[copies]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());

    const auto cPlayerCopies = [&] {
        size_t copies = 0;
        for (const uint32_t cId : host.CopiesOf())
            copies += host.Actors.at(cId).Player;
        return copies;
    };
    party.Until([&] { return cPlayerCopies() == 1; }, "the host's game makes a copy of the guest's player");

    WHEN("the guest leaves")
    {
        party.Leave(party.Guest());
        party.Until([&] { return cPlayerCopies() == 0; }, "the host's copy of the guest's player goes");
    }
}
