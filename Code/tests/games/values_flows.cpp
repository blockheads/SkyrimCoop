// Actor values across games (ActorValueService): an owner's health lost and regained shows on its copies.
#include "Party.h"
#include <Events/ReferenceLoadedEvent.h>
#include <Messages/Relayed.h>

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

SCENARIO("A value write reads and sends only that value on the next update", "[values]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guard has a copy");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    const auto cReads = host.ValueReads[{guard, 73}];
    const auto cOtherReads = host.ValueReads[{guard, 74}];
    party.As(party.Host(), [&] { host.SetActorValue(guard, 73, 50.f); host.SetActorValue(guard, 73, 60.f); });
    for (int i = 0; i < 3; ++i)
        party.Step(party.Host());
    CHECK(host.ValueReads[{guard, 73}] == cReads + 1);
    CHECK(host.ValueReads[{guard, 74}] == cOtherReads);
    party.Until([&] { return guest.GetActorValue(cCopy, 73) == 60.f; }, "the coalesced value reaches the copy");
    CHECK(guest.GetActorPermanentValue(cCopy, 73) == 60.f);
}

SCENARIO("A copy restores its owner's pools on writes without an owner resend", "[values]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] {
        guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f});
        host.ForceActorValue(guard, 0, FakeEngine::kHealth, 300.f);
        host.ForceActorValue(guard, 2, FakeEngine::kHealth, 250.f);
    });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guard has a copy");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    party.Until([&] { return guest.GetActorValue(cCopy, FakeEngine::kHealth) == 250.f; }, "the spawn converges");
    SECTION("a writer changes the pools")
    {
        party.As(party.Guest(), [&] {
            guest.ForceActorValue(cCopy, 0, FakeEngine::kHealth, 100.f);
            guest.ForceActorValue(cCopy, 2, FakeEngine::kHealth, 50.f);
        });
    }
    SECTION("a 3D load reconstructs values without the write funnel")
    {
        guest.Actors.at(cCopy).Values.ActorMaxValuesList[FakeEngine::kHealth] = 100.f;
        guest.Actors.at(cCopy).Values.ActorValuesList[FakeEngine::kHealth] = 50.f;
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(ReferenceLoadedEvent{cCopy}); });
    }
    // The owner is frozen: no periodic packet can repair this write in the copy's engine.
    for (int i = 0; i < 3; ++i)
        party.Step(party.Guest());
    CHECK(guest.GetActorPermanentValue(cCopy, FakeEngine::kHealth) == 300.f);
    CHECK(guest.GetActorValue(cCopy, FakeEngine::kHealth) == 250.f);
}

SCENARIO("Converged pools stay quiet and discrete values are not scanned", "[values]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host has the guest's player");
    const auto cSettled = std::chrono::steady_clock::now() + std::chrono::milliseconds(1100);
    party.Until([&] { return std::chrono::steady_clock::now() >= cSettled; }, "initial traffic settles");
    const auto cWrites = host.Actors.at(PlayerCopy(host)).ValueWrites;
    const auto cReads = guest.ValueReads[{FakeEngine::kPlayer, 73}];
    const auto cLater = std::chrono::steady_clock::now() + std::chrono::milliseconds(1100);
    party.Until([&] { return std::chrono::steady_clock::now() >= cLater; }, "a quiet second passes");
    CHECK(host.Actors.at(PlayerCopy(host)).ValueWrites == cWrites);
    CHECK(guest.ValueReads[{FakeEngine::kPlayer, 73}] == cReads);
}

SCENARIO("Pool packets received while a copy is absent apply when its 3D returns", "[values]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSmoke, {}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guard has a copy");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    uint32_t sessionId = 0;
    for (auto&& [cEntity, form, copy] : party.Guest().view<FormIdComponent, RemoteComponent>().each())
        if (form.Id == cCopy)
            sessionId = copy.Id;
    REQUIRE(sessionId);
    auto copy = guest.Actors.at(cCopy);
    guest.Actors.erase(cCopy);
    party.As(party.Guest(), [&] {
        NotifyActorValueChanges current;
        current.Id = sessionId;
        current.Values[FakeEngine::kHealth] = 250.f;
        party.Guest().GetDispatcher().trigger(current);
        NotifyActorMaxValueChanges maximum;
        maximum.Id = sessionId;
        maximum.Values[FakeEngine::kHealth] = 300.f;
        party.Guest().GetDispatcher().trigger(maximum);
    });
    guest.Actors.emplace(cCopy, std::move(copy));
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(ReferenceLoadedEvent{cCopy}); });
    for (int i = 0; i < 3; ++i)
        party.Step(party.Guest());
    CHECK(guest.GetActorPermanentValue(cCopy, FakeEngine::kHealth) == 300.f);
    CHECK(guest.GetActorValue(cCopy, FakeEngine::kHealth) == 250.f);
}
