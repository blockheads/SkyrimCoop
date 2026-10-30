// Equipment across games (InventoryService): what an owner's actor equips or unequips, its copies wear. Each ends
// with the actor checksum (Party::Agree), which compares what each copy wears with its owner's actor.
#include "Party.h"

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

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

// That game's copy of the other game's player.
uint32_t PlayerCopy(const FakeEngine& acEngine)
{
    for (const uint32_t cId : acEngine.CopiesOf())
        if (acEngine.Actors.at(cId).Player)
            return cId;
    return 0;
}

bool Wears(const FakeEngine& acEngine, uint32_t aActor, uint32_t aItem, bool aLeft = false)
{
    for (const auto& [cMod, cBase, cLeft] : acEngine.Worn(aActor))
        if (cBase == aItem && cLeft == aLeft)
            return true;
    return false;
}
} // namespace

SCENARIO("A player's equipment reaches its copy", "[equipment]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) && PlayerCopy(guest); }, "each game has the other's player");
    party.Agree("the games agree on the players");

    WHEN("the guest's player draws an iron sword")
    {
        party.As(party.Guest(), [&] { guest.Equips(FakeEngine::kPlayer, kIronSword); });
        party.Until([&] { return Wears(host, PlayerCopy(host), kIronSword); }, "the host's copy holds the sword");
        party.Agree("the games agree on the guest's sword");

        AND_WHEN("it takes a steel sword in its place")
        {
            party.As(party.Guest(), [&] { guest.Equips(FakeEngine::kPlayer, kSteelSword); });
            party.Until([&] { return Wears(host, PlayerCopy(host), kSteelSword); }, "the host's copy holds the steel");
            party.Agree("the games agree on the steel sword alone");
        }
        AND_WHEN("it puts the sword away")
        {
            party.As(party.Guest(), [&] { guest.Unequips(FakeEngine::kPlayer, kIronSword); });
            party.Until([&] { return !Wears(host, PlayerCopy(host), kIronSword); }, "the host's copy lets it go");
            party.Agree("the games agree on empty hands");
        }
        AND_WHEN("it takes a dagger in its left hand")
        {
            party.As(party.Guest(), [&] { guest.Equips(FakeEngine::kPlayer, kIronDagger, FakeEngine::kLeftHand); });
            party.Until([&] { return Wears(host, PlayerCopy(host), kIronDagger, true); },
                        "the host's copy holds the dagger left");
            party.Agree("the games agree on both hands");
        }
    }

    WHEN("the host's player puts on iron armor and a helmet, then takes the helmet off")
    {
        party.As(party.Host(), [&] {
            host.Equips(FakeEngine::kPlayer, kIronArmor, 0);
            host.Equips(FakeEngine::kPlayer, kIronHelmet, 0);
        });
        party.Until([&] { return Wears(guest, PlayerCopy(guest), kIronHelmet); }, "the guest's copy wears the helmet");
        party.Agree("the games agree on the iron armor and the helmet");
        party.As(party.Host(), [&] { host.Unequips(FakeEngine::kPlayer, kIronHelmet, 0); });
        party.Until([&] { return !Wears(guest, PlayerCopy(guest), kIronHelmet); }, "the guest's copy takes it off");
        party.Agree("the games agree on the iron armor alone");
    }

    WHEN("the host's player swaps iron armor for steel, its helmet on")
    {
        party.As(party.Host(), [&] {
            host.Equips(FakeEngine::kPlayer, kIronArmor, 0);
            host.Equips(FakeEngine::kPlayer, kIronHelmet, 0);
        });
        party.Until([&] { return Wears(guest, PlayerCopy(guest), kIronHelmet); }, "the guest's copy wears the helmet");
        party.As(party.Host(), [&] { host.Equips(FakeEngine::kPlayer, kSteelArmor, 0); });
        party.Until([&] { return Wears(guest, PlayerCopy(guest), kSteelArmor); }, "the guest's copy wears the steel");
        party.Agree("the games agree on the steel armor and the helmet");
    }
}

SCENARIO("A host NPC's equipment reaches its copy", "[equipment]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest's game has a copy of the guard");
    party.Agree("the games agree on the guard");

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
    Party party(1, Plugins);
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
    Party party(1, Plugins, false);
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

SCENARIO("A player's drawn weapon reaches its copy", "[equipment]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) && PlayerCopy(guest); }, "each game has the other's player");

    // In Skyrim the owner's draw and sheathe actions replay on the copy; the fake has no graphs, so this is the
    // service's fallback: the owner's hands (DrawWeaponRequest) and the copy converging to them.
    party.As(party.Guest(), [&] { guest.Draws(FakeEngine::kPlayer, true); });
    party.Until([&] { return host.GetWeaponState(PlayerCopy(host)) == WeaponState::Drawn; }, "the host's copy draws");
    party.Agree("the games agree on the guest's hands up");
    party.As(party.Guest(), [&] { guest.Draws(FakeEngine::kPlayer, false); });
    party.Until([&] { return host.GetWeaponState(PlayerCopy(host)) == WeaponState::Sheathed; },
                "the host's copy sheathes");
    party.Agree("the games agree on the guest's hands down");
}

SCENARIO("A guest joining sees the host's player with its weapon drawn", "[equipment]")
{
    Party party(1, Plugins, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.Draws(FakeEngine::kPlayer, true); });

    party.Join(party.Guest());
    party.Until([&] { return PlayerCopy(guest) != 0; }, "the guest's game has the host's player");
    party.Agree("the guest's copy of the host's player has its hands up");
}
