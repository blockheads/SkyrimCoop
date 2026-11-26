// Items as their owner's part (docs/design/one-simulator.md, piece 3): a copy holds what its owner's actor holds, by
// count, whatever reached it first.
#include "Party.h"
#include "Fixtures.h"
#include <items/Overrides.h>
#include <items/ContainerTransfers.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kIronSword = 0x00012EB7;
constexpr uint32_t kBandit = 0x0003DE8D;      // a bandit whose base lists leveled lists, as Skyrim's does
constexpr uint32_t kBanditList = 0x00037C1B;  // one of them (LVLI)
constexpr uint32_t kGuard = 0xD8C56, kGuardBase = 0x3DEE0;

void BanditWorld(FakeEngine& aEngine)
{
    SmokeWorld(aEngine);
    aEngine.ItemForms[kBanditList] = {FakeEngine::kLeveledItem};
    aEngine.BaseContainers[kBandit][kBanditList] = 1;
}

// A guard placed in the plugins carrying a sword; on the guest its 3D is not loaded yet.
void GuardWorld(FakeEngine& aEngine)
{
    SmokeWorld(aEngine);
    aEngine.PlaceNpc(kGuard, kGuardBase, kSmoke, {});
    aEngine.Actors.at(kGuard).Items.Entries.push_back({.BaseId = GameId(0, kIronSword), .Count = 1});
    if (aEngine.Name != "Dovahkiin")
        aEngine.Actors.at(kGuard).Loaded = false;
}

bool Wears(const FakeEngine& acEngine, uint32_t aActor, uint32_t aItem)
{
    return std::ranges::any_of(acEngine.Worn(aActor), [aItem](const auto& acWorn) { return std::get<1>(acWorn) == aItem; });
}
} // namespace

SCENARIO("An equip sent after the spawn was read survives it", "[items][items-part]")
{
    Party party(1, GuardWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.IsActorCopy(kGuard); }, "the guest's guard is the host's copy, its 3D not in");

    // Read after the spawn: the owner equips the sword it carried.
    party.As(party.Host(), [&] { host.Equips(kGuard, kIronSword); });
    int frames = 0;
    party.Until([&] { return ++frames > 120; }, "the equip has had time to reach the guest");
    party.As(party.Guest(), [&] { guest.Load3D(kGuard); });
    party.Until([&] { return guest.ActorHas3D(kGuard); }, "the copy's 3D is in");
    for (int i = 0; i < 3; ++i)
        party.Frame();
    CHECK(Wears(guest, kGuard, kIronSword)); // the spawn read before the equip must not take it off
    party.Agree("the copy wears what its owner wears");
}

SCENARIO("A looted corpse's copy is as empty as the corpse", "[items][items-part]")
{
    const bool cGuestLoots = GENERATE(false, true);
    Party party(1, BanditWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t bandit{};
    party.As(party.Host(), [&] {
        bandit = host.LoadNpc(kBandit, kSmoke, {30.f, 40.f, 0.f});
        host.GetsItem(bandit, kIronSword, 2);
    });
    const auto cCopy = [&] { return guest.CopiesOf(kBandit).empty() ? 0u : guest.CopiesOf(kBandit)[0]; };
    party.Until([&] { return cCopy() && guest.CountOf(cCopy(), kIronSword) == 2; }, "the guest sees the bandit's swords");
    party.As(party.Host(), [&] { host.Hit(bandit, 500.f); });
    party.Until([&] { return guest.IsActorDead(cCopy()); }, "the bandit dies on both games");

    if (cGuestLoots)
        party.As(party.Guest(), [&] {
            Inventory::Entry item;
            item.BaseId = GameId(0, kIronSword);
            item.Count = 2;
            REQUIRE(ContainerTransfers::Propose(party.Guest(), cCopy(), FakeEngine::kPlayer, item));
        });
    else
        party.As(party.Host(), [&] {
            host.LosesItem(bandit, kIronSword, 2);
            host.GetsItem(FakeEngine::kPlayer, kIronSword, 2);
        });
    party.Until([&] { return host.CountOf(bandit, kIronSword) == 0 && guest.CountOf(cCopy(), kIronSword) == 0; },
                "the swords leave the corpse on both games");
    party.Agree("the corpse's copy holds what the corpse holds, its base's leveled lists once");
}

SCENARIO("A copy whose hands the engine's outfit reset emptied holds its owner's weapon again", "[items][items-part][hand-weapon]")
{
    // 418622 empties the process's hands (39811-39813) and leaves the weapon's worn flag (measured: Aela's torch).
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host has the guest's player");
    party.As(party.Guest(), [&] {
        guest.GetsItem(FakeEngine::kPlayer, kIronSword, 1);
        guest.Equips(FakeEngine::kPlayer, kIronSword, FakeEngine::kRightHand);
    });
    const uint32_t cCopy = PlayerCopy(host);
    party.Until([&] { return host.GetEquippedWeaponFormId(cCopy, 1) == kIronSword; }, "the copy holds the guest's sword");
    party.As(party.Host(), [&] { host.RebuildCopyEquipment(cCopy, kIronSword); });
    REQUIRE(Wears(host, cCopy, kIronSword));
    REQUIRE(host.GetEquippedWeaponFormId(cCopy, 1) == 0);
    party.Until([&] { return host.GetEquippedWeaponFormId(cCopy, 1) == kIronSword; }, "the copy holds the sword again");
}

namespace
{
constexpr uint32_t kHuntingBow = 0x13985;

std::array<uint32_t, 2> Hands(const FakeEngine& acEngine, uint32_t aActor)
{
    return {acEngine.GetEquippedWeaponFormId(aActor, 0), acEngine.GetEquippedWeaponFormId(aActor, 1)};
}
} // namespace

SCENARIO("A copy left holding its bow in one hand holds it in both again", "[items][items-part][hand-weapon][bow]")
{
    // rejoined-guest-sees-rider: Faendal's copy at [0, bow] at its first repair after a rejoin, against his owner's
    // [bow, bow], and the repair checked the right hand only (measured 2026-11-23). hand-slots.md.
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host has the guest's player");
    party.As(party.Guest(), [&] {
        guest.GetsItem(FakeEngine::kPlayer, kHuntingBow, 1);
        guest.Equips(FakeEngine::kPlayer, kHuntingBow, FakeEngine::kRightHand);
    });
    REQUIRE(Hands(guest, FakeEngine::kPlayer) == std::array<uint32_t, 2>{kHuntingBow, kHuntingBow});
    const uint32_t cCopy = PlayerCopy(host);
    party.Until([&] { return Hands(host, cCopy) == std::array<uint32_t, 2>{kHuntingBow, kHuntingBow}; }, "the copy holds the bow in both hands");
    party.As(party.Host(), [&] {
        host.LeavesTwoHanderInRightOnly(cCopy);
        host.Unload3D(cCopy);
        host.Load3D(cCopy);
    });
    party.Until([&] { return Hands(host, cCopy) == std::array<uint32_t, 2>{kHuntingBow, kHuntingBow}; },
                "the copy holds its bow in both hands again");
}

SCENARIO("A copy holding a torch beside its bow settles as its owner holds them", "[items][items-part][hand-weapon][bow]")
{
    // Aela's owner held [torch, bow] (inventory-changes.md, measured): the bow keeps the right hand only, and a repair
    // asking both hands of it would strip and re-equip the copy forever.
    constexpr uint32_t cTorch = 0x1D4EC;
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host has the guest's player");
    party.As(party.Guest(), [&] {
        guest.GetsItem(FakeEngine::kPlayer, kHuntingBow, 1);
        guest.GetsItem(FakeEngine::kPlayer, cTorch, 1);
        guest.Equips(FakeEngine::kPlayer, kHuntingBow, FakeEngine::kRightHand);
        guest.Equips(FakeEngine::kPlayer, cTorch, 0);
    });
    REQUIRE(Hands(guest, FakeEngine::kPlayer) == std::array<uint32_t, 2>{cTorch, kHuntingBow});
    const uint32_t cCopy = PlayerCopy(host);
    party.Until([&] { return Hands(host, cCopy) == std::array<uint32_t, 2>{cTorch, kHuntingBow}; }, "the copy holds the torch and the bow");
    const int cResets = host.Actors.at(cCopy).EquipmentResets;
    party.As(party.Host(), [&] {
        host.Unload3D(cCopy);
        host.Load3D(cCopy);
    });
    for (int i = 0; i < 300; ++i)
        party.Frame();
    CHECK(Hands(host, cCopy) == std::array<uint32_t, 2>{cTorch, kHuntingBow});
    CHECK(host.Actors.at(cCopy).EquipmentResets == cResets); // settled: its 3D load needs no strip
}

SCENARIO("A host script taking an item from a guest's character takes it on the guest's game", "[items][items-part]")
{
    // Lucan's hand-in (quest-gold-turn-in-guest): the host's engine changes its copy of the guest's player; the guest's
    // game, its owner, runs the change and its part carries it on.
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host has the guest's player");
    party.As(party.Guest(), [&] { guest.GetsItem(FakeEngine::kPlayer, kIronSword, 1); });
    const uint32_t cCopy = PlayerCopy(host);
    party.Until([&] { return host.CountOf(cCopy, kIronSword) == 1; }, "the host's copy carries the guest's sword");
    party.As(party.Host(), [&] { host.LosesItem(cCopy, kIronSword, 1); });
    party.Until([&] { return guest.CountOf(FakeEngine::kPlayer, kIronSword) == 0; }, "the guest's own player loses the sword");
    party.Agree("the copy carries what its owner does");
}

namespace
{
// A party reward on a guest's player and on the host's copy of it, as the party store puts it in each (party-items.md).
Inventory::Entry PartySword()
{
    return {.BaseId = GameId(0, kIronSword), .Count = 1, .ExtraName = "[P] Iron Sword"};
}

uint32_t GivePartySword(Party& aParty)
{
    auto& host = Party::Engine(aParty.Host());
    auto& guest = Party::Engine(aParty.Guest());
    aParty.Until([&] { return PlayerCopy(host) != 0; }, "the host has the guest's player");
    const uint32_t cCopy = PlayerCopy(host);
    aParty.As(aParty.Guest(), [&] {
        ScopedInventoryOverride _;
        guest.GetsItem(FakeEngine::kPlayer, PartySword());
    });
    aParty.As(aParty.Host(), [&] {
        ScopedInventoryOverride _;
        host.GetsItem(cCopy, PartySword());
    });
    return cCopy;
}

int32_t Tagged(const FakeEngine& acEngine, uint32_t aActor)
{
    int32_t count = 0;
    for (const auto& cStack : acEngine.EntriesOf(aActor, kIronSword))
        if (cStack.ExtraName == PartySword().ExtraName)
            count += cStack.Count;
    return count;
}
} // namespace

SCENARIO("A player's copy keeps its party sword when its owner gets a plain one", "[items][items-part][party-items]")
{
    // Review of PR #83: a player's party stacks are the party store's, so the copy's items apply only the private ones,
    // even while its owner's part does not name the reward yet (the store reached the copy first).
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host has the guest's player");
    const uint32_t cCopy = PlayerCopy(host);
    party.As(party.Host(), [&] {
        ScopedInventoryOverride _;
        host.GetsItem(cCopy, PartySword());
    });
    party.As(party.Guest(), [&] { guest.GetsItem(FakeEngine::kPlayer, kIronSword, 1); });
    party.Until([&] { return host.CountOf(cCopy, kIronSword) == 2; }, "the host's copy carries both swords");
    CHECK(Tagged(host, cCopy) == 1);
}

SCENARIO("A player's copy wears the party sword its owner equips", "[items][items-part][party-items]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    const uint32_t cCopy = GivePartySword(party);
    party.As(party.Guest(), [&] { guest.Equips(FakeEngine::kPlayer, kIronSword); });
    party.Until([&] { return Wears(host, cCopy, kIronSword); }, "the host's copy wears the party sword");
    party.As(party.Guest(), [&] { guest.Unequips(FakeEngine::kPlayer, kIronSword); });
    party.Until([&] { return !Wears(host, cCopy, kIronSword); }, "the host's copy takes it off");
    CHECK(Tagged(host, cCopy) == 1);
}

SCENARIO("A player's copy selects the shout its owner selects, switches and clears it", "[items][items-part][shout]")
{
    // Review of PR #83: a shout has no worn flag; the items part carries it beside the hands.
    constexpr uint32_t kUnrelentingForce = 0x13E07, kWhirlwindSprint = 0x2F7BA;
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host has the guest's player");
    const uint32_t cCopy = PlayerCopy(host);
    const auto cShout = [&] { return host.Actors.at(cCopy).Items.CurrentMagicEquipment.Shout; };
    for (const GameId& cWanted : {GameId(0, kUnrelentingForce), GameId(0, kWhirlwindSprint), GameId{}})
    {
        party.As(party.Guest(), [&] { guest.EquipsShout(FakeEngine::kPlayer, cWanted); });
        party.Until([&] { return cShout() == cWanted; }, std::format("the host's copy selects shout {:X}", cWanted.BaseId));
    }
}
