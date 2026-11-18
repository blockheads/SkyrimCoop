// Objects across games (ObjectService): an actor's activation of an object replays on the other games, by the
// activator's copy; a locked one's is a lockpick attempt, not passed on.
#include "Party.h"
#include "Fixtures.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kLever = 0x000F1B2C; // a placed reference in QASmoke
constexpr uint32_t kGate = 0x000F1B2D;  // a locked one
constexpr uint32_t kChest = 0x000C2CE8; // QASmoke's small chest, a lid that opens

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
    aEngine.References[kLever] = {.Cell = kSmoke};
    aEngine.References[kGate] = {.Cell = kSmoke, .Locked = true, .LockLevel = 50};
    aEngine.References[kChest] = {.Cell = kSmoke, .Chest = true};
}


} // namespace

SCENARIO("A guest pulling a lever pulls it on the host's game, by its copy", "[objects]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");

    party.As(party.Guest(), [&] {
        guest.Activates(FakeEngine::kPlayer, kGate);
        guest.Activates(FakeEngine::kPlayer, kLever);
    });
    party.Until([&] { return std::ranges::count(host.Activations, std::pair{kLever, PlayerCopy(host)}) == 1; },
                "the host's copy of the guest pulls the lever");
    THEN("the locked gate's activation was a lockpick attempt, not passed on")
    CHECK(std::ranges::count(host.Activations, std::pair{kGate, PlayerCopy(host)}) == 0);
}

// Scenario guest-chest-lid-after-disable; docs/re/systems/guest-world-events.md, "A lid after a disable".
SCENARIO("A chest the host disables under a guest's open menu shows one lid once enabled", "[objects]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");

    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, kChest); });
    party.Until([&] { return std::ranges::count(host.Activations, std::pair{kChest, PlayerCopy(host)}) == 1; },
                "the host's copy of the guest opens the chest");
    REQUIRE(guest.References.at(kChest).Lid == 1.f);
    REQUIRE(host.References.at(kChest).Lid == 0.f);

    party.As(party.Host(), [&] { host.ScriptSetsEnabled(kChest, false); });
    party.Until([&] { return guest.References.at(kChest).Disabled; }, "the guest's chest is disabled");
    party.As(party.Guest(), [&] { guest.ClosesChestMenu(); }); // the live menu's auto-close
    party.As(party.Host(), [&] { host.ScriptSetsEnabled(kChest, true); });
    party.Until([&] { return !guest.References.at(kChest).Disabled; }, "the guest's chest is enabled");
    party.As(party.Guest(), [&] { guest.Finishes3D(kChest); });
    party.Until([&] { return guest.References.at(kChest).Lid == 0.f && host.References.at(kChest).Lid == 0.f; },
                "both lids closed, as both chests' open states read");
    CHECK_FALSE(guest.References.at(kChest).Open);
}

// Scenario host-menus-close-when-target-goes: the same on the host's own chest.
SCENARIO("A chest the host disables under its own open menu shows one lid once enabled", "[objects]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(guest) != 0; }, "the guest's game has the host's player");

    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, kChest); });
    party.Until([&] { return std::ranges::count(guest.Activations, std::pair{kChest, PlayerCopy(guest)}) == 1; },
                "the guest's copy of the host opens the chest");
    REQUIRE(host.References.at(kChest).Lid == 1.f);

    party.As(party.Host(), [&] {
        host.ScriptSetsEnabled(kChest, false);
        host.ClosesChestMenu(); // the live menu's auto-close
        host.ScriptSetsEnabled(kChest, true);
        host.Finishes3D(kChest);
    });
    party.Until([&] { return !guest.References.at(kChest).Disabled; }, "the guest's chest is enabled");
    party.As(party.Guest(), [&] { guest.Finishes3D(kChest); });
    party.Until([&] { return host.References.at(kChest).Lid == 0.f && guest.References.at(kChest).Lid == 0.f; },
                "both lids closed, as both chests' open states read");
}
