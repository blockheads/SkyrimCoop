// Items as their owner's part (docs/design/one-simulator.md, piece 3): a copy holds what its owner's actor holds, by
// count, whatever reached it first.
#include "Party.h"
#include "Fixtures.h"
#include <Services/ContainerTransfers.h>

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
