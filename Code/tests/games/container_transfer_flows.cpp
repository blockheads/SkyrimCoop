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
