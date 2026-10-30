// Items across games (InventoryService): what an owner's actor gets, loses or drops, its copies carry; a player's
// items stay its own (docs/design/party-model.md), its gold is the party's (purse_flows.cpp). Each ends with the actor checksum (Party::Agree), which
// compares what each copy carries with its owner's actor.
#include "Party.h"
#include "Fixtures.h"

#include <Components.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGold = 0x0000000F;
constexpr uint32_t kIronSword = 0x00012EB7;

// That game's copies of other games' players.



bool Wears(const FakeEngine& acEngine, uint32_t aActor, uint32_t aItem)
{
    for (const auto& [cMod, cBase, cLeft] : acEngine.Worn(aActor))
        if (cBase == aItem)
            return true;
    return false;
}
} // namespace

SCENARIO("A guest joining later gets another guest's player as it is now", "[items]")
{
    Party party(2, SmokeWorld, false);
    FakeEngine& first = Party::Engine(party.Guest(0));
    FakeEngine& second = Party::Engine(party.Guest(1));
    party.Join(party.Guest(0));
    party.Until([&] { return PlayerCopies(Party::Engine(party.Host())).size() == 1; },
                "the host's game has the first guest's player");

    party.As(party.Guest(0), [&] {
        first.GetsItem(FakeEngine::kPlayer, kGold, 70);
        first.GetsItem(FakeEngine::kPlayer, kIronSword, 1);
        first.Equips(FakeEngine::kPlayer, kIronSword);
    });
    FakeEngine& host = Party::Engine(party.Host());
    party.Until(
        [&] {
            const uint32_t cCopy = PlayerCopies(host)[0];
            return host.CountOf(cCopy, kGold) == 70 && Wears(host, cCopy, kIronSword);
        },
        "the host's copy carries the first guest's gold and holds its sword");

    party.Join(party.Guest(1));
    party.Until([&] { return PlayerCopies(second).size() == 2; }, "the second guest's game has both other players");
    party.Agree("every copy carries and wears what its owner does");
}

SCENARIO("What a player does right after its game announces it reaches its copy", "[items]")
{
    Party party(1, SmokeWorld, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Join(party.Guest());

    // Only the guest's game runs: it announces its player under the id from its join, the host has not read it yet.
    const auto cAnnounced = [&] {
        for (const auto cEntity : party.Guest().view<FormIdComponent, LocalComponent>())
            if (party.Guest().get<FormIdComponent>(cEntity).Id == FakeEngine::kPlayer)
                return true;
        return false;
    };
    party.Until(cAnnounced, "the guest's player is announced", {}, &party.Guest());
    party.As(party.Guest(), [&] {
        guest.GetsItem(FakeEngine::kPlayer, kGold, 30);
        guest.GetsItem(FakeEngine::kPlayer, kIronSword, 1);
        guest.Equips(FakeEngine::kPlayer, kIronSword);
    });
    party.Step(party.Guest()); // its runner hands the changes to InventoryService, which sends them at once
    REQUIRE(PlayerCopies(host).empty());

    party.Until([&] { return PlayerCopies(host).size() == 1; }, "the host's game has the guest's player");
    party.Agree("the host's copy carries the gold and holds the sword");
}
