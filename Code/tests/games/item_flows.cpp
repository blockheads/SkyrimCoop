// Items across games (InventoryService): what an owner's actor gets, loses or drops, its copies carry; a player's
// items stay its own (docs/design/party-model.md). Each ends with the actor checksum (Party::Agree), which
// compares what each copy carries with its owner's actor.
#include "Party.h"

#include <Components.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint32_t kGold = 0x0000000F;
constexpr uint32_t kIronSword = 0x00012EB7;
constexpr uint32_t kApple = 0x00064B31;

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

// That game's copies of other games' players.
std::vector<uint32_t> PlayerCopies(const FakeEngine& acEngine)
{
    std::vector<uint32_t> copies;
    for (const uint32_t cId : acEngine.CopiesOf())
        if (acEngine.Actors.at(cId).Player)
            copies.push_back(cId);
    return copies;
}

bool Wears(const FakeEngine& acEngine, uint32_t aActor, uint32_t aItem)
{
    for (const auto& [cMod, cBase, cLeft] : acEngine.Worn(aActor))
        if (cBase == aItem)
            return true;
    return false;
}
} // namespace

SCENARIO("A player's items reach its copy and stay its own", "[items]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopies(host).size() == 1 && PlayerCopies(guest).size() == 1; },
                "each game has the other's player");
    const auto cCopy = [&] { return PlayerCopies(host)[0]; };

    party.As(party.Guest(), [&] { guest.GetsItem(FakeEngine::kPlayer, kGold, 100); });
    party.Until([&] { return host.CountOf(cCopy(), kGold) == 100; }, "the host's copy of the guest carries its gold");
    THEN("the host's player has none of it") CHECK(host.CountOf(FakeEngine::kPlayer, kGold) == 0);
    party.Agree("the games agree on the gold");

    WHEN("the guest's player spends some")
    {
        party.As(party.Guest(), [&] { guest.LosesItem(FakeEngine::kPlayer, kGold, 40); });
        party.Until([&] { return host.CountOf(cCopy(), kGold) == 60; }, "the copy carries what is left");
        party.Agree("the games agree on the gold left");
    }

    WHEN("the guest's player draws a sword and drops it")
    {
        party.As(party.Guest(), [&] {
            guest.GetsItem(FakeEngine::kPlayer, kIronSword, 1);
            guest.Equips(FakeEngine::kPlayer, kIronSword);
        });
        party.Until([&] { return Wears(host, cCopy(), kIronSword); }, "the copy holds the sword");
        party.As(party.Guest(), [&] { guest.Drops(FakeEngine::kPlayer, kIronSword, 1); });
        party.Until([&] { return host.CountOf(cCopy(), kIronSword) == 0; }, "the copy no longer has it");
        THEN("nor holds it")
        {
            CHECK(!Wears(host, cCopy(), kIronSword));
            party.Agree("the games agree on the empty hands");
        }
    }

    WHEN("the host's player eats an apple of its own")
    {
        party.As(party.Host(), [&] {
            host.GetsItem(FakeEngine::kPlayer, kApple, 2);
            host.LosesItem(FakeEngine::kPlayer, kApple, 1);
        });
        party.Until([&] { return guest.CountOf(PlayerCopies(guest)[0], kApple) == 1; }, "the guest's copy has one");
        THEN("the guest's player has none") CHECK(guest.CountOf(FakeEngine::kPlayer, kApple) == 0);
        party.Agree("the games agree on the apple");
    }
}

SCENARIO("A host NPC's items reach its copy", "[items]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest's game has a copy of the guard");
    party.As(party.Host(), [&] { host.GetsItem(guard, kGold, 25); });
    party.Until([&] { return guest.CountOf(guest.CopiesOf(kGuard)[0], kGold) == 25; }, "the copy carries the gold");
    party.Agree("the games agree on the guard's gold");
}

SCENARIO("A guest joining later gets another guest's player as it is now", "[items]")
{
    Party party(2, Plugins, false);
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
    Party party(1, Plugins, false);
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
