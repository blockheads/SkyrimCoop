// Player-made potions and poisons (docs/design/crafted-potions.md): each game makes its own form for one, the session
// names it by its recipe, so it travels as any item does: to its brewer's copies, through chests, dropped, on a weapon.
// A character record keeps them (join_flows.cpp).
#include "Party.h"

#include <Services/ContainerTransfers.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kChest = 0x0001A001; // a chest in kSmoke
constexpr uint32_t kIronSword = 0x00012EB7;

void Plugins(FakeEngine& aEngine)
{
    aEngine.References[kChest] = {.Cell = kSmoke};
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

// Fortify Health, Restore Health, Damage Health (a poison): magnitude, area, duration, cost, effect.
Recipe Fortify()
{
    return {false, {{25.f, 0, 60, 30.f, GameId(0, 0x3EAF3)}}};
}

Recipe Restore()
{
    return {false, {{50.f, 0, 0, 20.f, GameId(0, 0x3EB15)}}};
}

Recipe Venom()
{
    return {true, {{10.f, 0, 5, 20.f, GameId(0, 0x3EB42)}}};
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

// How many of a recipe's potions that game's actor carries.
int32_t Holds(const FakeEngine& acEngine, uint32_t aActor, const Recipe& acRecipe)
{
    int32_t count = 0;
    for (const auto& cEntry : acEngine.Actors.at(aActor).Items.Entries)
        if (cEntry.BaseId == acRecipe.Id())
            count += cEntry.Count;
    return count;
}

// That game's own form for a recipe (0: it made none).
uint32_t FormFor(const FakeEngine& acEngine, const Recipe& acRecipe)
{
    for (const auto& [cId, cRecipe] : acEngine.Brewed)
        if (cRecipe == acRecipe)
            return cId;
    return 0;
}

// How many of a recipe's potions the chest holds in that game.
int32_t InChest(const FakeEngine& acEngine, const Recipe& acRecipe)
{
    const auto& cItems = acEngine.References.at(kChest).Items;
    const auto it = cItems.find(FormFor(acEngine, acRecipe));
    return it != cItems.end() ? it->second : 0;
}

// The container menu (container_transfer_flows.cpp): a guest's proposed to the host, the host's moved here. aCount > 0
// takes from the chest.
void Transfer(World& aGame, uint32_t aItem, int32_t aCount)
{
    auto& engine = Party::Engine(aGame);
    Inventory::Entry item;
    item.BaseId = ItemId(aItem).value_or(GameId{});
    item.Count = std::abs(aCount);
    if (!engine.DecidesWorld && ContainerTransfers::Propose(aGame, aCount > 0 ? kChest : FakeEngine::kPlayer,
                                                            aCount > 0 ? FakeEngine::kPlayer : kChest, item))
        return;
    engine.GetsItem(FakeEngine::kPlayer, aItem, aCount);
    engine.PlayerSetsItems(kChest, aItem, engine.References.at(kChest).Items[aItem] - aCount);
}
} // namespace

SCENARIO("A guest's brewed potion reaches its copy on the host, the host's own form for its recipe", "[items][potions]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopies(host).size() == 1 && PlayerCopies(guest).size() == 1; }, "each game has the other's player");

    // Each game numbers its created forms itself: the host brewed something else first.
    party.As(party.Host(), [&] { host.PlayerBrews(Restore(), 1); });
    party.As(party.Guest(), [&] { guest.PlayerBrews(Fortify(), 3); });
    party.Until([&] { return Holds(host, PlayerCopies(host)[0], Fortify()) == 3; }, "the host's copy of the guest carries its three potions");

    THEN("the host made the potion for itself, under its own id")
    {
        CHECK(FormFor(host, Fortify()) != 0);
        CHECK(FormFor(host, Fortify()) != FormFor(guest, Fortify()));
        CHECK(Holds(guest, PlayerCopies(guest)[0], Restore()) == 1);
    }
    party.Agree("the games agree on the potions");
}

SCENARIO("A guest stores brewed potions in a chest; the host's game holds them and its player takes them", "[items][potions][container-transfer]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopies(host).size() == 1; }, "the host has the guest's player");

    uint32_t brewed = 0;
    party.As(party.Host(), [&] { host.PlayerBrews(Restore(), 1); });
    party.As(party.Guest(), [&] { brewed = guest.PlayerBrews(Fortify(), 2); });
    party.Until([&] { return Holds(host, PlayerCopies(host)[0], Fortify()) == 2; }, "the host's copy carries the potions");
    party.As(party.Guest(), [&] { Transfer(party.Guest(), brewed, -2); });
    party.Until([&] { return InChest(host, Fortify()) == 2 && Holds(guest, FakeEngine::kPlayer, Fortify()) == 0; },
                "the host's chest holds the guest's two potions", [&] {
                    std::string why = std::format("host chest {} guest holds {} copy holds {};", InChest(host, Fortify()),
                                                  Holds(guest, FakeEngine::kPlayer, Fortify()), Holds(host, PlayerCopies(host)[0], Fortify()));
                    for (const auto& [cId, cCount] : host.References.at(kChest).Items)
                        why += std::format(" {:X}x{}", cId, cCount);
                    for (const auto& [cId, cRecipe] : host.Brewed)
                        why += std::format(" brewed {:X}", cId);
                    return why;
                });

    party.As(party.Host(), [&] { Transfer(party.Host(), FormFor(host, Fortify()), 2); });
    party.Until([&] { return Holds(host, FakeEngine::kPlayer, Fortify()) == 2 && InChest(guest, Fortify()) == 0; },
                "the host's player takes them, gone from the guest's chest");
    party.Agree("the games agree on who carries the potions");
}

SCENARIO("A guest drops a brewed potion; the host's world makes it and its player picks it up", "[items][potions][loose]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.PlayerBrews(Restore(), 1); });
    Inventory::Entry potion;
    party.As(party.Guest(), [&] {
        potion.BaseId = ItemId(guest.PlayerBrews(Fortify(), 1)).value_or(GameId{});
        potion.Count = 1;
        guest.DropsInWorld(potion);
    });
    party.Until([&] { return host.Loose.size() == 1; }, "the host's world has the guest's potion");
    party.As(party.Host(), [&] { host.PicksUp(host.Loose.begin()->first); });
    party.Until([&] { return host.Loose.empty() && guest.Loose.empty(); }, "it leaves both worlds");
    CHECK(Holds(host, FakeEngine::kPlayer, Fortify()) == 1);
}

SCENARIO("A guest's weapon coated with its brewed poison keeps the coating on its copy", "[items][potions]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopies(host).size() == 1; }, "the host has the guest's player");

    party.As(party.Guest(), [&] {
        Inventory::Entry sword;
        sword.BaseId = SessionIdOf(kIronSword);
        sword.Count = 1;
        sword.ExtraPoisonId = ItemId(guest.Brew(Venom())).value_or(GameId{});
        sword.ExtraPoisonCount = 3;
        guest.GetsItem(FakeEngine::kPlayer, sword);
    });
    const auto cCoating = [&] {
        for (const auto& cEntry : host.Actors.at(PlayerCopies(host)[0]).Items.Entries)
            if (cEntry.BaseId == SessionIdOf(kIronSword))
                return std::pair(cEntry.ExtraPoisonId, cEntry.ExtraPoisonCount);
        return std::pair(GameId{}, 0u);
    };
    party.Until([&] { return cCoating().first == Venom().Id(); }, "the copy's sword carries the guest's poison");
    CHECK(cCoating().second == 3);
    THEN("the host has the poison as its own") CHECK(host.Brewed.at(FormFor(host, Venom())).IsPoison);
}

SCENARIO("A guest joining later gets the recipes of what the others carry", "[items][potions]")
{
    Party party(2, Plugins, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& first = Party::Engine(party.Guest(0));
    FakeEngine& late = Party::Engine(party.Guest(1));
    party.Join(party.Guest(0));
    party.As(party.Guest(0), [&] { first.PlayerBrews(Fortify(), 2); });
    party.Until([&] { return !PlayerCopies(host).empty() && Holds(host, PlayerCopies(host)[0], Fortify()) == 2; },
                "the host's copy of the first guest carries its potions");

    party.Join(party.Guest(1));
    party.Until(
        [&] {
            for (const uint32_t cCopy : PlayerCopies(late))
                if (Holds(late, cCopy, Fortify()) == 2)
                    return true;
            return false;
        },
        "the late guest's copy of the first guest carries the potions");
    uint32_t made = 0;
    party.As(party.Guest(1), [&] { made = ItemForm(Fortify().Id()); });
    CHECK(late.Brewed.at(made) == Fortify());
}
