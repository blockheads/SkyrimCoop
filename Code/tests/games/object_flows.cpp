// Objects across games (ObjectService): a guest's activation of an object runs on the host's game, by its copy; the
// other games get what changed as the host's rows. A locked one's is a lockpick attempt, not passed on.
#include "Party.h"
#include "Fixtures.h"

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

SCENARIO("A lever a guest pulls moves once, on the host, and no other guest replays it", "[objects]")
{
    Party party(2, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest(0));
    FakeEngine& other = Party::Engine(party.Guest(1));
    party.Until([&] { return PlayerCopy(host, 1) != 0 && PlayerCopy(other) != 0; }, "every game has every player");

    party.As(party.Guest(0), [&] { guest.Activates(FakeEngine::kPlayer, kLever); });
    party.Until([&] { return host.Activations.size() == 1; }, "the host's game runs the guest's pull");
    // A marker from the host after it: the other guest has had whatever came before.
    party.As(party.Host(), [&] { host.PlayerSetsLock(kGate, false, 50); });
    party.Until([&] { return !other.References[kGate].Locked; }, "the other guest has the host's later change");

    THEN("the other guest replayed nothing")
    CHECK(other.Activations.empty());
}
