// Equipment across games (InventoryService): what an owner's actor equips or unequips, its copies wear. Each ends
// with the actor checksum (Party::Agree), which compares what each copy wears with its owner's actor.
#include "Party.h"
#include "Fixtures.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint32_t kIronSword = 0x00012EB7;
constexpr uint32_t kSteelSword = 0x00013989;
constexpr uint32_t kIronDagger = 0x0001397E;
constexpr uint32_t kIronArmor = 0x00012E49;
constexpr uint32_t kIronHelmet = 0x00012E4D;
constexpr uint32_t kSteelArmor = 0x00013952;

// That game's copy of the other game's player.


bool Wears(const FakeEngine& acEngine, uint32_t aActor, uint32_t aItem, bool aLeft = false)
{
    for (const auto& [cMod, cBase, cLeft] : acEngine.Worn(aActor))
        if (cBase == aItem && cLeft == aLeft)
            return true;
    return false;
}
} // namespace

TEST_CASE("Players carry, equip, swap, draw and remove their own items", "[items][equipment]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) && PlayerCopy(guest); }, "each game has the other's player");
    party.Agree("the games agree on the players");
    const uint32_t cGuestCopy = PlayerCopy(host);
    const uint32_t cHostCopy = PlayerCopy(guest);

    INFO("carry and consume personal items");
    party.As(party.Guest(), [&] { guest.GetsItem(FakeEngine::kPlayer, 0xA, 10); });
    party.Until([&] { return host.CountOf(cGuestCopy, 0xA) == 10; }, "the copy carries ten lockpicks");
    CHECK(host.CountOf(FakeEngine::kPlayer, 0xA) == 0);
    party.Agree("the games agree on the lockpicks");
    party.As(party.Guest(), [&] { guest.LosesItem(FakeEngine::kPlayer, 0xA, 4); });
    party.Until([&] { return host.CountOf(cGuestCopy, 0xA) == 6; }, "the copy carries six lockpicks left");
    party.Agree("the games agree on the lockpicks left");

    INFO("equip and ready the guest's sword");
    party.As(party.Guest(), [&] {
        guest.GetsItem(FakeEngine::kPlayer, kIronSword, 1);
        guest.Equips(FakeEngine::kPlayer, kIronSword);
    });
    party.Until([&] { return Wears(host, cGuestCopy, kIronSword); }, "the host's copy holds the sword");
    party.Agree("the games agree on the guest's sword");
    // The fake has no graphs: retain the service's draw-state fallback check.
    party.As(party.Guest(), [&] { guest.Draws(FakeEngine::kPlayer, true); });
    party.Until([&] { return host.GetWeaponState(cGuestCopy) == WeaponState::Drawn; }, "the copy draws");
    party.Agree("the games agree on the guest's hands up");
    party.As(party.Guest(), [&] { guest.Draws(FakeEngine::kPlayer, false); });
    party.Until([&] { return host.GetWeaponState(cGuestCopy) == WeaponState::Sheathed; }, "the copy sheathes");
    party.Agree("the games agree on the guest's hands down");

    INFO("swap the right hand, put it away, then equip both hands");
    party.As(party.Guest(), [&] { guest.Equips(FakeEngine::kPlayer, kSteelSword); });
    party.Until([&] { return Wears(host, cGuestCopy, kSteelSword); }, "the copy holds the steel sword");
    party.Agree("the games agree on the steel sword alone");
    party.As(party.Guest(), [&] { guest.Equips(FakeEngine::kPlayer, kIronSword); });
    party.Until([&] { return Wears(host, cGuestCopy, kIronSword); }, "the copy holds the iron sword again");
    party.As(party.Guest(), [&] { guest.Unequips(FakeEngine::kPlayer, kIronSword); });
    party.Until([&] { return !Wears(host, cGuestCopy, kIronSword); }, "the copy lets the sword go");
    party.Agree("the games agree on empty hands");
    party.As(party.Guest(), [&] { guest.Equips(FakeEngine::kPlayer, kIronSword); });
    party.Until([&] { return Wears(host, cGuestCopy, kIronSword); }, "the sword is back before the left hand equips");
    party.As(party.Guest(), [&] { guest.Equips(FakeEngine::kPlayer, kIronDagger, FakeEngine::kLeftHand); });
    party.Until([&] { return Wears(host, cGuestCopy, kIronDagger, true); }, "the copy holds the dagger left");
    party.Agree("the games agree on both hands");
    party.As(party.Guest(), [&] { guest.Drops(FakeEngine::kPlayer, kIronSword, 1); });
    party.Until([&] { return host.CountOf(cGuestCopy, kIronSword) == 0; }, "the copy no longer carries the dropped sword");
    CHECK(!Wears(host, cGuestCopy, kIronSword));
    party.Agree("the games agree after dropping the sword");

    INFO("dress the host, remove its helmet, then swap armor with the helmet on");
    party.As(party.Host(), [&] {
        host.Equips(FakeEngine::kPlayer, kIronArmor, 0);
        host.Equips(FakeEngine::kPlayer, kIronHelmet, 0);
    });
    party.Until([&] { return Wears(guest, cHostCopy, kIronHelmet); }, "the copy wears the helmet");
    party.Agree("the games agree on iron armor and the helmet");
    party.As(party.Host(), [&] { host.Unequips(FakeEngine::kPlayer, kIronHelmet, 0); });
    party.Until([&] { return !Wears(guest, cHostCopy, kIronHelmet); }, "the copy takes the helmet off");
    party.Agree("the games agree on iron armor alone");
    party.As(party.Host(), [&] { host.Equips(FakeEngine::kPlayer, kIronHelmet, 0); });
    party.Until([&] { return Wears(guest, cHostCopy, kIronHelmet); }, "the copy wears its helmet again");
    party.As(party.Host(), [&] { host.Equips(FakeEngine::kPlayer, kSteelArmor, 0); });
    party.Until([&] { return Wears(guest, cHostCopy, kSteelArmor); }, "the copy wears steel armor");
    party.Agree("the games agree on steel armor and the helmet");

    party.As(party.Host(), [&] {
        host.GetsItem(FakeEngine::kPlayer, 0x64B31, 2);
        host.LosesItem(FakeEngine::kPlayer, 0x64B31, 1);
    });
    party.Until([&] { return guest.CountOf(cHostCopy, 0x64B31) == 1; }, "the copy carries one apple");
    CHECK(guest.CountOf(FakeEngine::kPlayer, 0x64B31) == 0);
    party.Agree("the games agree on the apple");
}

TEST_CASE("A host NPC carries gold and equips its sword", "[items][equipment]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest's game has a copy of the guard");
    party.Agree("the games agree on the guard");
    party.As(party.Host(), [&] { host.GetsItem(guard, 0xF, 25); });
    party.Until([&] { return guest.CountOf(guest.CopiesOf(kGuard)[0], 0xF) == 25; }, "the copy carries the gold");
    party.Agree("the games agree on the guard's gold");


    party.As(party.Host(), [&] { host.Equips(guard, kSteelSword); });
    party.Until([&] { return Wears(guest, guest.CopiesOf(kGuard)[0], kSteelSword); }, "the guard's copy draws it");
    party.Agree("the games agree on the guard's sword");

    THEN("the copy takes nothing it is not sent")
    {
        const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
        party.As(party.Guest(), [&] { guest.Equips(cCopy, kIronDagger, FakeEngine::kLeftHand); });
        CHECK(!Wears(guest, cCopy, kIronDagger, true));
    }
}

SCENARIO("An NPC's outfit put back on with no item change still dresses its copy", "[equipment]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] {
        guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f});
        host.GetsItem(guard, kSteelArmor, 1);
        host.Equips(guard, kSteelArmor, 0);
    });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && Wears(guest, guest.CopiesOf(kGuard)[0], kSteelArmor); },
                "the guard's copy wears its armor");

    // removeallitems: the armor comes off and out, each change raised; then the engine dresses it in its outfit again.
    party.As(party.Host(), [&] { host.LosesItem(guard, kSteelArmor, 1); });
    party.Until([&] { return !Wears(guest, guest.CopiesOf(kGuard)[0], kSteelArmor); }, "the copy takes the armor off");
    party.As(party.Host(), [&] { host.OutfitPutsOn(guard, kSteelArmor); });
    party.Until([&] { return Wears(guest, guest.CopiesOf(kGuard)[0], kSteelArmor); }, "the copy wears the outfit again");
    party.Agree("the games agree on the dressed guard");
}

SCENARIO("A guest joining gets the host's player as it is dressed", "[equipment]")
{
    Party party(1, SmokeWorld, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] {
        host.Equips(FakeEngine::kPlayer, kSteelSword);
        host.Equips(FakeEngine::kPlayer, kIronArmor, 0);
    });

    party.Join(party.Guest());
    party.Until([&] { return PlayerCopy(guest) != 0; }, "the guest's game has the host's player");
    THEN("its copy wears the sword and the armor")
    {
        party.Agree("the games agree on the host's player");
        CHECK(Wears(guest, PlayerCopy(guest), kSteelSword));
        CHECK(Wears(guest, PlayerCopy(guest), kIronArmor));
    }
}

SCENARIO("A guest joining sees the host's player with its weapon drawn", "[equipment]")
{
    Party party(1, SmokeWorld, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.Draws(FakeEngine::kPlayer, true); });

    party.Join(party.Guest());
    party.Until([&] { return PlayerCopy(guest) != 0; }, "the guest's game has the host's player");
    party.Agree("the guest's copy of the host's player has its hands up");
}
