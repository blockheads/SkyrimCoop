#include "Party.h"
#include "Fixtures.h"
#include <Games/Overrides.h>
#include <Services/ContainerTransfers.h>
#include <Services/CopyRecords.h>
#include <SessionIds.h>

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

// guest-barter-follows-merchant-stock: barter merges the merchant's own lists into its stock (0x14023D080), and the copy's
// reconcile frees one as the loot menu's did (docs/re/systems/item-menus.md).
SCENARIO("A guest's open barter never reads a stack the merchant copy's reconcile freed", "[container-transfer][item-menu]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t npc = 0;
    const Inventory::Entry cBlade{.BaseId = GameId(0, kSword), .Count = 1, .ExtraName = "Merchant's Blade"};
    party.As(party.Host(), [&] {
        npc = host.LoadNpc(0x1A67C, kCell, {30.f, 40.f, 0.f});
        host.GetsItem(npc, cBlade);
    });
    const auto cCopy = [&] { return guest.CopiesOf(0x1A67C).empty() ? 0u : guest.CopiesOf(0x1A67C)[0]; };
    party.Until([&] { return cCopy() && guest.CountOf(cCopy(), kSword) == 1; }, "the merchant's copy carries the named sword");
    party.As(party.Guest(), [&] { guest.BarterMerchant = cCopy(); });
    party.As(party.Host(), [&] { host.LosesItem(npc, cBlade); });
    party.Until([&] { return guest.CountOf(cCopy(), kSword) == 0; }, "the copy's reconcile takes the sword");
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

namespace
{
// MS13 (the Golden Claw): its GoldenClaw alias creates the claw in Arvel; the claw's script sets 40 when a player gets
// it (MS13GoldenClawScript). Playtest 2026-11-22, item 15.
constexpr uint32_t kGoldenClawQuest = 0x39645, kArvel = 0x39646, kClaw = 0x39647, kArvelsJournal = 0x39654;

struct ArvelsBody
{
    Party party;
    FakeEngine& host;
    FakeEngine& guest;
    uint32_t arvel = 0;

    ArvelsBody()
        : party(1, [](FakeEngine& aEngine) {
            Plugins(aEngine);
            aEngine.FormNames[kClaw] = "Golden Claw";
            QuestState quest;
            quest.Id = GameId{0, kGoldenClawQuest};
            quest.Flags = 1;
            quest.CurrentStage = 30;
            quest.DoneStages = {10, 30};
            aEngine.Quests[kGoldenClawQuest] = quest;
        })
        , host(Party::Engine(party.Host()))
        , guest(Party::Engine(party.Guest()))
    {
        party.As(party.Host(), [&] {
            arvel = host.LoadNpc(kArvel, kCell, {30.f, 40.f, 0.f});
            host.AliasCreatesIn(arvel, kClaw, {.Quest = kGoldenClawQuest, .Stage = 40});
        });
        party.Until(
            [&] {
                return PlayerCopies(host).size() == 1 && PlayerCopies(guest).size() == 1 && guest.CopiesOf(kArvel).size() == 1 &&
                       guest.CountOf(guest.CopiesOf(kArvel)[0], kClaw) == 1;
            },
            "the guest sees Arvel carrying the claw");
    }

    // Every player and copy carries one claw, and MS13 stands at the stage in both games.
    void Until(uint16_t aStage, const char* acpWhat)
    {
        const auto cClaws = [&] {
            return std::format("host player {}, host's copy {}, guest player {}, guest's copy {}, Arvel {}; MS13 host {} guest {}",
                               host.CountOf(FakeEngine::kPlayer, kClaw), host.CountOf(PlayerCopies(host)[0], kClaw),
                               guest.CountOf(FakeEngine::kPlayer, kClaw), guest.CountOf(PlayerCopies(guest)[0], kClaw), host.CountOf(arvel, kClaw),
                               host.Quests.at(kGoldenClawQuest).CurrentStage, guest.Quests[kGoldenClawQuest].CurrentStage);
        };
        party.Until(
            [&] {
                return host.Quests.at(kGoldenClawQuest).CurrentStage == aStage && guest.Quests[kGoldenClawQuest].CurrentStage == aStage &&
                       host.CountOf(arvel, kClaw) == 0 && host.CountOf(FakeEngine::kPlayer, kClaw) == 1 &&
                       host.CountOf(PlayerCopies(host)[0], kClaw) == 1 && guest.CountOf(FakeEngine::kPlayer, kClaw) == 1 &&
                       guest.CountOf(PlayerCopies(guest)[0], kClaw) == 1;
            },
            acpWhat, cClaws);
    }
};
} // namespace

SCENARIO("The host takes a quest alias's item from a body: its quest moves on, every player carries it", "[container-transfer][party-items]")
{
    ArvelsBody body;
    body.party.As(body.party.Host(), [&] { body.host.MovesStack(body.arvel, FakeEngine::kPlayer, kClaw, 1); });
    body.Until(40, "MS13 reaches 40 in both games and every player carries the claw");
}

SCENARIO("A guest takes a quest alias's item from a body: the host's quest moves on, every player carries it", "[container-transfer][party-items]")
{
    // The host's take for a guest removed the claw to nowhere: its alias and script were gone, MS13 stayed at 30.
    ArvelsBody body;
    body.party.As(body.party.Guest(), [&] {
        const auto cClaw = body.guest.EntriesOf(body.guest.CopiesOf(kArvel)[0], kClaw).at(0);
        REQUIRE(ContainerTransfers::Propose(body.party.Guest(), body.guest.CopiesOf(kArvel)[0], FakeEngine::kPlayer, cClaw));
    });
    body.Until(40, "MS13 reaches 40 in both games and every player carries the claw");
    THEN("the host's player holds the quest item, the alias with it")
    {
        CHECK(body.host.AliasStacks.contains({FakeEngine::kPlayer, kClaw}));
    }
}

namespace
{
constexpr uint32_t kBorkulsKey = 0xABB00; // a key an alias holds, not a quest object (MS02's BorkulKey)

// Every player and copy carries aCount of the item, the host's player's with its alias, named "[P] <name>".
void ExpectShared(ArvelsBody& aBody, uint32_t aItem, const char* acpTagged, int32_t aCount = 1)
{
    const auto cCount = [aItem](const FakeEngine& acEngine, uint32_t aHolder) { return acEngine.CountOf(aHolder, aItem); };
    const auto cAt = [&] {
        return std::format("host {} copy {} guest {} copy {} alias {}", cCount(aBody.host, FakeEngine::kPlayer), cCount(aBody.host, PlayerCopies(aBody.host)[0]),
                           cCount(aBody.guest, FakeEngine::kPlayer), cCount(aBody.guest, PlayerCopies(aBody.guest)[0]),
                           aBody.host.AliasStacks.contains({FakeEngine::kPlayer, aItem}));
    };
    const auto cOneEach = [&] {
        return cCount(aBody.host, FakeEngine::kPlayer) == aCount && cCount(aBody.host, PlayerCopies(aBody.host)[0]) == aCount &&
               cCount(aBody.guest, FakeEngine::kPlayer) == aCount && cCount(aBody.guest, PlayerCopies(aBody.guest)[0]) == aCount;
    };
    aBody.party.Until(cOneEach, "every player and copy carries it", cAt);
    const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    aBody.party.Until([&] { return std::chrono::steady_clock::now() > cUntil; }, "a while passes");
    INFO(cAt());
    CHECK(cOneEach());
    if (aCount == 0)
        return;
    CHECK(aBody.host.AliasStacks.contains({FakeEngine::kPlayer, aItem}));
    CHECK(aBody.host.EntriesOf(FakeEngine::kPlayer, aItem).at(0).ExtraName == acpTagged);
}

void ArvelHasKey(ArvelsBody& aBody)
{
    for (FakeEngine* pEngine : {&aBody.host, &aBody.guest})
    {
        pEngine->FormNames[kBorkulsKey] = "Borkul's Key";
        pEngine->ItemForms[kBorkulsKey].Type = 45; // KEYM
    }
    aBody.party.As(aBody.party.Host(), [&] {
        aBody.host.AliasCreatesIn(aBody.arvel, kBorkulsKey, {.Quest = kGoldenClawQuest, .Stage = 37, .QuestObject = false});
    });
    aBody.party.Until([&] { return aBody.guest.CountOf(aBody.guest.CopiesOf(kArvel)[0], kBorkulsKey) == 1; }, "the guest sees the key");
}
// MS13's ArvelsJournal alias holds Arvel's journal in him: not a quest object (stage 37 when a player gets it here).
void ArvelHasJournal(ArvelsBody& aBody)
{
    for (FakeEngine* pEngine : {&aBody.host, &aBody.guest})
        pEngine->FormNames[kArvelsJournal] = "Arvel's Journal";
    aBody.party.As(aBody.party.Host(), [&] {
        aBody.host.AliasCreatesIn(aBody.arvel, kArvelsJournal, {.Quest = kGoldenClawQuest, .Stage = 37, .QuestObject = false});
    });
    aBody.party.Until([&] { return aBody.guest.CountOf(aBody.guest.CopiesOf(kArvel)[0], kArvelsJournal) == 1; }, "the guest sees the journal");
}

void GuestTakesJournal(ArvelsBody& aBody)
{
    aBody.party.As(aBody.party.Guest(), [&] {
        const auto cJournal = aBody.guest.EntriesOf(aBody.guest.CopiesOf(kArvel)[0], kArvelsJournal).at(0);
        REQUIRE(ContainerTransfers::Propose(aBody.party.Guest(), aBody.guest.CopiesOf(kArvel)[0], FakeEngine::kPlayer, cJournal));
    });
}
} // namespace

SCENARIO("A guest takes an alias's item that is not a quest object: the party's, its alias on the host's player", "[container-transfer][party-items]")
{
    ArvelsBody body;
    ArvelHasJournal(body);
    GuestTakesJournal(body);
    ExpectShared(body, kArvelsJournal, "[P] Arvel's Journal");
    CHECK(body.host.Quests.at(kGoldenClawQuest).CurrentStage == 37);
}

SCENARIO("A guest pickpockets an alias's item: the party's, its alias on the host's player", "[container-transfer][party-items]")
{
    ArvelsBody body;
    ArvelHasJournal(body);
    body.party.As(body.party.Guest(), [&] { body.guest.Pickpockets(body.guest.CopiesOf(kArvel)[0], kArvelsJournal, false); });
    ExpectShared(body, kArvelsJournal, "[P] Arvel's Journal");
    THEN("the real one is Arvel's: stolen")
    {
        CHECK(body.host.EntriesOf(FakeEngine::kPlayer, kArvelsJournal).at(0).ExtraOwner == GameId(0, kArvel));
    }
}

SCENARIO("A guest drops a party item an alias holds and picks it up again: the host's real one, alias and all", "[container-transfer][party-items][drops]")
{
    // Skyrim 2026-11-23: the host made a new journal for the guest's drop, and the alias's went to nowhere.
    ArvelsBody body;
    ArvelHasJournal(body);
    GuestTakesJournal(body);
    ExpectShared(body, kArvelsJournal, "[P] Arvel's Journal");
    body.party.As(body.party.Guest(), [&] { body.guest.DropsInWorld(body.guest.EntriesOf(FakeEngine::kPlayer, kArvelsJournal).at(0)); });
    ExpectShared(body, kArvelsJournal, "", 0);
    body.party.Until([&] { return body.host.Loose.size() == 1; }, "the journal lies in the host's world");
    CHECK(body.host.Loose.begin()->second.Alias.has_value());
    body.party.Until([&] { return body.guest.Loose.size() == 1 && body.guest.HostDecides.size() == 1; }, "the guest's drop is the host's");
    body.party.As(body.party.Guest(), [&] { body.guest.PicksUp(body.guest.Loose.begin()->first); });
    ExpectShared(body, kArvelsJournal, "[P] Arvel's Journal");
    CHECK(body.host.Loose.empty());
}

SCENARIO("The host takes a key an alias holds: the party's, its alias kept", "[container-transfer][party-items]")
{
    ArvelsBody body;
    ArvelHasKey(body);
    body.party.As(body.party.Host(), [&] { body.host.MovesStack(body.arvel, FakeEngine::kPlayer, kBorkulsKey, 1); });
    ExpectShared(body, kBorkulsKey, "[P] Borkul's Key");
}

SCENARIO("A guest takes a key an alias holds: the party's, its alias kept on the host's player", "[container-transfer][party-items]")
{
    ArvelsBody body;
    ArvelHasKey(body);
    body.party.As(body.party.Guest(), [&] {
        const auto cKey = body.guest.EntriesOf(body.guest.CopiesOf(kArvel)[0], kBorkulsKey).at(0);
        REQUIRE(ContainerTransfers::Propose(body.party.Guest(), body.guest.CopiesOf(kArvel)[0], FakeEngine::kPlayer, cKey));
    });
    ExpectShared(body, kBorkulsKey, "[P] Borkul's Key");
}

SCENARIO("A guest stores a party item an alias holds in a chest and takes it back: the host's real one, alias and all", "[container-transfer][party-items]")
{
    // The host's put moved the guest's copy's mirror into the chest, and the real one went to nowhere.
    ArvelsBody body;
    ArvelHasJournal(body);
    GuestTakesJournal(body);
    ExpectShared(body, kArvelsJournal, "[P] Arvel's Journal");
    body.party.As(body.party.Guest(), [&] {
        const auto cJournal = body.guest.EntriesOf(FakeEngine::kPlayer, kArvelsJournal).at(0);
        REQUIRE(ContainerTransfers::Propose(body.party.Guest(), FakeEngine::kPlayer, kChest, cJournal));
    });
    ExpectShared(body, kArvelsJournal, "", 0);
    body.party.Until([&] { return body.host.ContainerCountOf(kChest, kArvelsJournal) == 1 && body.guest.ContainerCountOf(kChest, kArvelsJournal) == 1; },
                     "the chest holds the journal on both games");
    CHECK(body.host.AliasStacks.contains({kChest, kArvelsJournal}));
    body.party.As(body.party.Guest(), [&] {
        Inventory::Entry journal;
        journal.BaseId = GameId(0, kArvelsJournal);
        journal.Count = 1;
        journal.ExtraName = "[P] Arvel's Journal";
        REQUIRE(ContainerTransfers::Propose(body.party.Guest(), kChest, FakeEngine::kPlayer, journal));
    });
    ExpectShared(body, kArvelsJournal, "[P] Arvel's Journal");
    CHECK(body.host.ContainerCountOf(kChest, kArvelsJournal) == 0);
}

SCENARIO("A guest's store of a party item the host's player no longer holds stores nothing", "[container-transfer][party-items]")
{
    // The review of PR #118: the put fell through to the guest's copy's mirror, putting a copy of the party's item away.
    ArvelsBody body;
    ArvelHasJournal(body);
    GuestTakesJournal(body);
    ExpectShared(body, kArvelsJournal, "[P] Arvel's Journal");
    // Another player stored the last real one first: the host's player holds none, the guest's count not yet told.
    body.party.As(body.party.Host(), [&] {
        ScopedInventoryOverride _;
        Inventory::Entry real = body.host.EntriesOf(FakeEngine::kPlayer, kArvelsJournal).at(0);
        real.Count = -1;
        body.host.GetsItem(FakeEngine::kPlayer, real);
    });
    body.party.As(body.party.Guest(), [&] {
        const auto cJournal = body.guest.EntriesOf(FakeEngine::kPlayer, kArvelsJournal).at(0);
        REQUIRE(ContainerTransfers::Propose(body.party.Guest(), FakeEngine::kPlayer, kChest, cJournal));
    });
    const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    body.party.Until([&] { return std::chrono::steady_clock::now() > cUntil; }, "the store is handled");
    CHECK(body.host.ContainerCountOf(kChest, kArvelsJournal) == 0);
    CHECK(body.guest.ContainerCountOf(kChest, kArvelsJournal) == 0);
}

SCENARIO("A guest stores a quest object it carries as the party's: the host's real one goes in, alias and all", "[container-transfer][party-items]")
{
    ArvelsBody body;
    body.party.As(body.party.Guest(), [&] {
        const auto cClaw = body.guest.EntriesOf(body.guest.CopiesOf(kArvel)[0], kClaw).at(0);
        REQUIRE(ContainerTransfers::Propose(body.party.Guest(), body.guest.CopiesOf(kArvel)[0], FakeEngine::kPlayer, cClaw));
    });
    body.Until(40, "MS13 reaches 40 and every player carries the claw");
    body.party.As(body.party.Guest(), [&] {
        const auto cClaw = body.guest.EntriesOf(FakeEngine::kPlayer, kClaw).at(0);
        REQUIRE(ContainerTransfers::Propose(body.party.Guest(), FakeEngine::kPlayer, kChest, cClaw));
    });
    body.party.Until([&] { return body.host.ContainerCountOf(kChest, kClaw) == 1 && body.host.CountOf(FakeEngine::kPlayer, kClaw) == 0; },
                     "the host's claw is in the chest");
    CHECK(body.host.AliasStacks.contains({kChest, kClaw}));
}

SCENARIO("A guest takes a key from a chest while carrying its own: it keeps its own and gets the party's", "[container-transfer][party-items]")
{
    // The review of PR #118: the host told the taker to rename a key it never got, renaming the one it owned instead.
    static constexpr uint32_t kKey = 0x00093B08; // Key to Breezehome
    Party party(1, [](FakeEngine& aEngine) {
        Plugins(aEngine);
        aEngine.References[kChest].Items[kKey] = 1;
        aEngine.FormNames[kKey] = "Key to Breezehome";
        aEngine.ItemForms[kKey].Type = 45; // KEYM
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopies(host).size() == 1 && guest.ContainerCountOf(kChest, kKey) == 1; }, "the guest sees the chest's key");
    party.As(party.Guest(), [&] {
        ScopedInventoryOverride _;
        guest.GetsItem(FakeEngine::kPlayer, kKey, 1); // its own, from its save
        Inventory::Entry key;
        key.BaseId = GameId(0, kKey);
        key.Count = 1;
        REQUIRE(ContainerTransfers::Propose(party.Guest(), kChest, FakeEngine::kPlayer, key));
    });
    const auto cOf = [&](bool aTagged) {
        int32_t count = 0;
        for (const auto& cEntry : guest.EntriesOf(FakeEngine::kPlayer, kKey))
            count += (cEntry.ExtraName == "[P] Key to Breezehome") == aTagged ? cEntry.Count : 0;
        return count;
    };
    party.Until([&] { return cOf(true) == 1 && cOf(false) == 1; }, "the guest carries its own key and the party's",
                [&] { return std::format("own {}, party's {}", cOf(false), cOf(true)); });
}

SCENARIO("A guest drops a party item an alias holds where the host's player cannot: refused, the real one kept", "[container-transfer][party-items][drops]")
{
    // The review of PR #118: in another interior the real journal went to nowhere and a new one was made.
    ArvelsBody body;
    ArvelHasJournal(body);
    GuestTakesJournal(body);
    ExpectShared(body, kArvelsJournal, "[P] Arvel's Journal");
    body.party.As(body.party.Host(), [&] { body.host.Actors.at(FakeEngine::kPlayer).Cell = 0x0001A26F; }); // another interior
    body.party.As(body.party.Guest(), [&] { body.guest.DropsInWorld(body.guest.EntriesOf(FakeEngine::kPlayer, kArvelsJournal).at(0)); });
    body.party.Until([&] { return body.guest.Loose.empty() && body.guest.CountOf(FakeEngine::kPlayer, kArvelsJournal) == 1; },
                     "the guest has its drop back", [&] {
                         return std::format("guest {} loose {}, host loose {}", body.guest.CountOf(FakeEngine::kPlayer, kArvelsJournal),
                                            body.guest.Loose.size(), body.host.Loose.size());
                     });
    ExpectShared(body, kArvelsJournal, "[P] Arvel's Journal");
    CHECK(body.host.Loose.empty());
}
