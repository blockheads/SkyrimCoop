// Objects across games (ObjectService): an actor's activation of an object replays on the other games, by the
// activator's copy; a locked one's is a lockpick attempt, not passed on.
#include "Party.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kLever = 0x000F1B2C; // a placed reference in QASmoke
constexpr uint32_t kGate = 0x000F1B2D;  // a locked one

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
    aEngine.References[kLever] = {.Cell = kSmoke};
    aEngine.References[kGate] = {.Cell = kSmoke, .Locked = true, .LockLevel = 50};
}

uint32_t PlayerCopy(const FakeEngine& acEngine)
{
    for (const uint32_t cId : acEngine.CopiesOf())
        if (acEngine.Actors.at(cId).Player)
            return cId;
    return 0;
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
