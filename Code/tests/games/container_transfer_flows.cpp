#include "Party.h"
#include "Fixtures.h"
#include <Services/ContainerTransfers.h>

namespace
{
constexpr uint32_t kCell = 0x31D2E, kChest = 0x1A001, kSword = 0x12EB7;
void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
    aEngine.References[kChest] = {.Cell = kCell, .Items = {{kSword, 1}}};
    aEngine.LoadCell(kCell);
}

// Native container-menu transfer: a guest's proposed to the host (TESObjectREFR's hook, off while the game decides its
// world), else removed from the source, added to the recipient, then its engine events.
void Transfer(World& aGame, int32_t aCount, uint32_t aItem = kSword)
{
    auto& engine = Party::Engine(aGame);
    Inventory::Entry item;
    item.BaseId = GameId(0, aItem);
    item.Count = std::abs(aCount);
    if (!engine.DecidesWorld && ContainerTransfers::Propose(aGame, aCount > 0 ? kChest : FakeEngine::kPlayer,
                                    aCount > 0 ? FakeEngine::kPlayer : kChest, item))
        return;
    engine.GetsItem(FakeEngine::kPlayer, aItem, aCount);
    engine.PlayerSetsItems(kChest, aItem, engine.References.at(kChest).Items[aItem] - aCount);
}

SCENARIO("A guest takes part of a stack and stores it again", "[container-transfer]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.PlayerSetsItems(kChest, kSword, 5); });
    party.Until([&] { return guest.References.at(kChest).Items[kSword] == 5; }, "the chest contains five swords");
    party.As(party.Guest(), [&] { Transfer(party.Guest(), 3); });
    party.Until(
        [&] {
            return guest.CountOf(FakeEngine::kPlayer, kSword) == 3 && guest.References.at(kChest).Items[kSword] == 2;
        },
        "the guest takes three");
    party.Agree("the copy carries the three granted swords");
    party.As(party.Guest(), [&] { Transfer(party.Guest(), -2); });
    party.Until(
        [&] {
            return guest.CountOf(FakeEngine::kPlayer, kSword) == 1 && guest.References.at(kChest).Items[kSword] == 4;
        },
        "the guest stores two");
    party.Agree("the inventories agree after storing");
}

SCENARIO("A guest takes a fresh roll's item: it gets it and the chest is empty on every game", "[container-transfer]")
{
    // guest-loots-hosts-roll: a chest keeps a taken roll item's entry at 0 (16059), which 16214 read as one: the host
    // measured nothing taken and its row put the item back (inventory-changes.md, "A roll's taken item").
    Party party(1, [](FakeEngine& aEngine) {
        Plugins(aEngine);
        aEngine.References.at(kChest).Rolled = {kSword};
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Guest(), [&] { Transfer(party.Guest(), 1); });
    party.Until([&] { return guest.CountOf(FakeEngine::kPlayer, kSword) == 1; }, "the guest gets the sword");
    party.Until([&] { return guest.ContainerCountOf(kChest, kSword) == 0; }, "the guest's chest is empty as the host's");
    CHECK(host.ContainerCountOf(kChest, kSword) == 0);
}

SCENARIO("A guest's theft from an owned chest is the owner's on the guest's game", "[container-transfer]")
{
    // Skyrim: 16059 marks a theft's item as the owner's only on the target it moves it to, which on the host was its
    // copy of the guest (inventory-changes.md, "What 16059's target does"). The grant carries the owner instead.
    constexpr uint32_t kOwner = 0x1347B;
    Party party(1, [](FakeEngine& aEngine) {
        Plugins(aEngine);
        aEngine.References.at(kChest).Owner = kOwner;
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    Inventory::Entry sword;
    sword.BaseId = GameId(0, kSword);
    sword.Count = 1;
    party.As(party.Guest(), [&] { REQUIRE(ContainerTransfers::Propose(party.Guest(), kChest, FakeEngine::kPlayer, sword, true)); });
    party.Until([&] { return guest.CountOf(FakeEngine::kPlayer, kSword) == 1; }, "the guest takes the sword");
    const auto cStacks = guest.EntriesOf(FakeEngine::kPlayer, kSword);
    REQUIRE(cStacks.size() == 1);
    CHECK(cStacks.front().ExtraOwner == GameId(0, kOwner));
    CHECK(host.References.at(kChest).Items[kSword] == 0);
    uint32_t copy{};
    for (const auto& [cId, cActor] : host.Actors)
        if (cActor.Player && cActor.Copy)
            copy = cId;
    REQUIRE(copy);
    party.Until([&] {
        const auto cCopyStacks = host.EntriesOf(copy, kSword);
        return cCopyStacks.size() == 1 && cCopyStacks.front().ExtraOwner == GameId(0, kOwner) && host.CountOf(copy, kSword) == 1;
    }, "the host's copy of the guest holds the one stolen sword its part says");
}

SCENARIO("Two stores of a guest's last sword before its items part arrives put one sword in the chest", "[container-transfer]")
{
    // A put is spent from the host's copy of the guest at once: the second finds nothing left (review of PR #83).
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.PlayerSetsItems(kChest, kSword, 0); });
    party.As(party.Guest(), [&] { guest.GetsItem(FakeEngine::kPlayer, kSword, 1); });
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host has the guest's player");
    const uint32_t copy = PlayerCopy(host);
    party.Until([&] { return host.CountOf(copy, kSword) == 1 && guest.References.at(kChest).Items[kSword] == 0; },
                "the host's copy of the guest carries its one sword");
    Inventory::Entry sword;
    sword.BaseId = GameId(0, kSword);
    sword.Count = 1;
    party.As(party.Guest(), [&] {
        REQUIRE(ContainerTransfers::Propose(party.Guest(), FakeEngine::kPlayer, kChest, sword));
        REQUIRE(ContainerTransfers::Propose(party.Guest(), FakeEngine::kPlayer, kChest, sword));
    });
    party.Until([&] { return guest.CountOf(FakeEngine::kPlayer, kSword) == 0 && host.References.at(kChest).Items[kSword] == 1; },
                "one sword leaves the guest for the chest");
    party.Agree("the chest and the guest agree after both stores");
    CHECK(host.References.at(kChest).Items[kSword] == 1);
}

SCENARIO("A guest taking container gold changes the party purse once", "[container-transfer]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.PlayerSetsItems(kChest, 0xF, 20); });
    party.Until([&] { return guest.References.at(kChest).Items[0xF] == 20; }, "the chest contains gold");
    const int32_t cBefore = host.CountOf(FakeEngine::kPlayer, 0xF);
    party.As(party.Guest(), [&] { Transfer(party.Guest(), 20, 0xF); });
    party.Until(
        [&] {
            return host.CountOf(FakeEngine::kPlayer, 0xF) == cBefore + 20 &&
                   guest.CountOf(FakeEngine::kPlayer, 0xF) == cBefore + 20;
        },
        "both players carry the new purse");
    party.Agree("all player copies carry the purse once");
    CHECK(host.References.at(kChest).Items[0xF] == 0);
}
int32_t Owned(Party& aParty, size_t aGuests)
{
    int32_t count = Party::Engine(aParty.Host()).CountOf(FakeEngine::kPlayer, kSword);
    for (size_t i = 0; i < aGuests; ++i)
        count += Party::Engine(aParty.Guest(i)).CountOf(FakeEngine::kPlayer, kSword);
    return count;
}
} // namespace

SCENARIO("A host and guest taking the last chest item do not duplicate it", "[container-transfer]")
{
    Party party(1, Plugins);
    party.Agree("the players are present");
    party.As(party.Guest(), [&] { Transfer(party.Guest(), 1); });
    party.As(party.Host(), [&] { Transfer(party.Host(), 1); });
    party.Until(
        [&] {
            return Party::Engine(party.Host()).References.at(kChest).Items[kSword] == 0 &&
                   Party::Engine(party.Guest()).References.at(kChest).Items[kSword] == 0;
        },
        "both chests are empty");
    CHECK(Owned(party, 1) == 1);
}

SCENARIO("Two guests taking the last chest item do not duplicate it", "[container-transfer]")
{
    Party party(2, Plugins);
    party.Agree("the players are present");
    party.As(party.Guest(0), [&] { Transfer(party.Guest(0), 1); });
    party.As(party.Guest(1), [&] { Transfer(party.Guest(1), 1); });
    party.Until([&] { return Party::Engine(party.Host()).References.at(kChest).Items[kSword] == 0; },
                "the host's chest is empty");
    party.Agree("the inventories reach their copies");
    CHECK(Owned(party, 2) == 1);
}

SCENARIO("A guest loots a host NPC's inventory once", "[container-transfer]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t npc = 0;
    party.As(party.Host(), [&] {
        npc = host.LoadNpc(0x1A67C, kCell, {30.f, 40.f, 0.f});
        host.GetsItem(npc, kSword, 2);
    });
    party.Until(
        [&] { return guest.CopiesOf(0x1A67C).size() == 1 && guest.CountOf(guest.CopiesOf(0x1A67C)[0], kSword) == 2; },
        "the guest sees the NPC's inventory");
    party.As(party.Guest(), [&] {
        Inventory::Entry item;
        item.BaseId = GameId(0, kSword);
        item.Count = 2;
        REQUIRE(ContainerTransfers::Propose(party.Guest(), guest.CopiesOf(0x1A67C)[0], FakeEngine::kPlayer, item));
    });
    party.Until(
        [&] {
            return guest.CountOf(FakeEngine::kPlayer, kSword) == 2 && host.CountOf(npc, kSword) == 0 &&
                   guest.CountOf(guest.CopiesOf(0x1A67C)[0], kSword) == 0;
        },
        "the guest loots both swords");
    party.Agree("the NPC and player inventories agree");
}

// guest-loot-menu-follows-corpse, the 2026-11-22 playtest's guest crashes: the take's reconcile freed the corpse's
// worn stack while the open menu still listed it (docs/re/systems/item-menus.md).
SCENARIO("A guest's open loot menu never reads a stack the copy's reconcile freed", "[container-transfer][item-menu]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t npc = 0;
    party.As(party.Host(), [&] {
        npc = host.LoadNpc(0x1A67C, kCell, {30.f, 40.f, 0.f});
        host.GetsItem(npc, kSword, 1);
        host.Equips(npc, kSword);
    });
    const auto cCopy = [&] { return guest.CopiesOf(0x1A67C).empty() ? 0u : guest.CopiesOf(0x1A67C)[0]; };
    const auto cWorn = [&] {
        const auto cEntries = guest.EntriesOf(cCopy(), kSword);
        return std::ranges::any_of(cEntries, [](const auto& acEntry) { return acEntry.ExtraWorn; });
    };
    party.Until([&] { return cCopy() && cWorn(); }, "the guest's copy wears the NPC's sword (a list of its own)");
    party.As(party.Host(), [&] { host.Hit(npc, 1000.f); });
    party.Until([&] { return guest.IsActorDead(cCopy()); }, "the copy dies");
    party.As(party.Guest(), [&] {
        guest.Activates(FakeEngine::kPlayer, cCopy());
        REQUIRE(guest.LootMenuOf == cCopy());
        // The menu's take: proposed to the host (ContainerTransfers), the copy changed when the host answers.
        Inventory::Entry item;
        item.BaseId = GameId(0, kSword);
        item.Count = 1;
        REQUIRE(ContainerTransfers::Propose(party.Guest(), cCopy(), FakeEngine::kPlayer, item));
    });
    party.Until([&] { return guest.CountOf(cCopy(), kSword) == 0 && guest.CountOf(FakeEngine::kPlayer, kSword) == 1; },
                "the host moves the sword: the copy's reconcile takes it off the corpse");
    party.As(party.Guest(), [&] { guest.HoversInItemMenu(); });
    CHECK(guest.ItemMenuFreedReads == 0);
}

SCENARIO("A guest taking a party item from a chest gives it to every player", "[container-transfer][party-items]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    Inventory::Entry tagged;
    tagged.BaseId = GameId(0, kSword);
    tagged.Count = 1;
    tagged.ExtraName = "[P] Iron Sword";
    party.As(party.Host(), [&] { host.PlayerSetsItems(kChest, kSword, 1, {tagged}); });
    party.Until([&] { return PlayerCopies(host).size() == 1 && PlayerCopies(guest).size() == 1 && guest.References.at(kChest).Items[kSword] == 1; },
                "the chest holds the party's sword on both games");
    // The guest's container menu takes it: proposed to the host (ContainerTransfers).
    party.As(party.Guest(), [&] { REQUIRE(ContainerTransfers::Propose(party.Guest(), kChest, FakeEngine::kPlayer, tagged)); });
    const auto cTagged = [](const FakeEngine& acEngine, uint32_t aActor) {
        int32_t count = 0;
        for (const auto& cEntry : acEngine.EntriesOf(aActor, kSword))
            if (cEntry.ExtraName == "[P] Iron Sword")
                count += cEntry.Count;
        return count;
    };
    party.Until(
        [&] {
            return cTagged(guest, FakeEngine::kPlayer) == 1 && cTagged(host, FakeEngine::kPlayer) == 1 &&
                   cTagged(host, PlayerCopies(host)[0]) == 1 && cTagged(guest, PlayerCopies(guest)[0]) == 1;
        },
        "every player and every copy carries it", [&] {
            return std::format("guest player {}, host player {}, host's copy {}, guest's copy {}", cTagged(guest, FakeEngine::kPlayer),
                               cTagged(host, FakeEngine::kPlayer), cTagged(host, PlayerCopies(host)[0]), cTagged(guest, PlayerCopies(guest)[0]));
        });
}

namespace
{
constexpr uint32_t kTributeChest = 0xF68AE, kGarnet = 0x63B42, kGold = 0xF;
void WithTributeChest(FakeEngine& aEngine)
{
    Plugins(aEngine);
    aEngine.ContainerBases.insert(kTributeChest);
}
} // namespace

SCENARIO("A chest the host's script places reaches a guest with what it rolled", "[container-transfer][created]")
{
    Party party(1, WithTributeChest);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.ScriptPlacesContainer(kCell, kTributeChest, {{kGarnet, 1}, {kGold, 50}}); });
    const auto cGuestChest = [&]() -> const FakeEngine::Reference* {
        for (const auto& [cId, cReference] : guest.References)
            if (cReference.Base == kTributeChest)
                return &cReference;
        return nullptr;
    };
    party.Until([&] { return cGuestChest() != nullptr; }, "the guest's game makes the chest");
    party.Until(
        [&] {
            const auto cItems = cGuestChest()->Items;
            return cItems.size() == 2 && cItems.contains(kGarnet) && cItems.at(kGarnet) == 1 && cItems.contains(kGold) &&
                   cItems.at(kGold) == 50;
        },
        "the guest's chest holds the host's garnet and 50 gold");
}
