// Actor values across games (ActorValueService): an owner's health lost and regained shows on its copies.
#include "Party.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

uint32_t PlayerCopy(const FakeEngine& acEngine)
{
    for (const uint32_t cId : acEngine.CopiesOf())
        if (acEngine.Actors.at(cId).Player)
            return cId;
    return 0;
}
} // namespace

SCENARIO("Health lost and regained shows on the copies", "[values]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");
    const auto cHealth = [](const FakeEngine& acEngine, uint32_t aId) {
        return acEngine.GetActorValue(aId, FakeEngine::kHealth);
    };
    const float cFull = cHealth(guest, FakeEngine::kPlayer);

    WHEN("the guest's player is hurt, then drinks a potion")
    {
        party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 30.f); });
        party.Until([&] { return cHealth(host, PlayerCopy(host)) == cFull - 30.f; }, "its copy shows the wound");
        party.As(party.Guest(), [&] { guest.ForceActorValue(FakeEngine::kPlayer, 2, FakeEngine::kHealth, cFull); });
        party.Until([&] { return cHealth(host, PlayerCopy(host)) == cFull; }, "its copy shows it healed");
    }

    WHEN("a host NPC is hurt")
    {
        uint32_t guard = 0;
        party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
        party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest's game has a copy of the guard");
        party.As(party.Host(), [&] { host.Hit(guard, 25.f); });
        party.Until([&] { return cHealth(guest, guest.CopiesOf(kGuard)[0]) == cHealth(host, guard); },
                    "its copy shows the wound");
        CHECK(cHealth(host, guard) < cFull);
    }
}
