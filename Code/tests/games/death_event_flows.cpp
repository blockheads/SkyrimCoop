#include "Party.h"
#include "Fixtures.h"

#include <Events/ActorDeathChangeEvent.h>
#include <Events/HealthChangeEvent.h>
#include <Messages/NotifyMessageFactory.h>
#include <Messages/Relayed.h>

namespace
{
constexpr uint32_t kSmoke = 0x31D2E;
constexpr uint32_t kGuard = 0x1A67C;

uint32_t SessionOf(World& aGame, uint32_t aFormId)
{
    for (auto&& [cEntity, form, copy] : aGame.view<FormIdComponent, RemoteComponent>().each())
        if (form.Id == aFormId)
            return copy.Id;
    return 0;
}
} // namespace

SCENARIO("A death state waits for its exact owner action, in either arrival order", "[death][values]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSmoke, {}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest has the guard");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    NotifyDeathStateChange death;
    death.Id = SessionOf(party.Guest(), cCopy);
    death.IsDead = true;
    death.DeathActionTick = 123;
    REQUIRE(death.Id);

    SECTION("the state arrives first and a refused replay falls back immediately")
    {
        party.As(party.Guest(), [&] {
            party.Guest().GetDispatcher().trigger(death);
            party.Guest().GetDispatcher().trigger(DeathActionReplayedEvent{cCopy, 122});
        });
        CHECK_FALSE(guest.IsActorDead(cCopy));
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(DeathActionReplayedEvent{cCopy, 123}); });
    }
    SECTION("the replay was already attempted when its state arrives")
    {
        party.As(party.Guest(), [&] {
            party.Guest().GetDispatcher().trigger(DeathActionReplayedEvent{cCopy, 123});
            party.Guest().GetDispatcher().trigger(death);
        });
    }
    SECTION("a death with no action applies without waiting")
    {
        death.DeathActionTick.reset();
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(death); });
    }
    CHECK(guest.IsActorDead(cCopy));
}

SCENARIO("A resurrection refused during dying retries on the copy's final death event", "[death][values]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSmoke, {}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest has the guard");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    const uint32_t cId = SessionOf(party.Guest(), cCopy);
    party.As(party.Guest(), [&] {
        NotifyActorValueChanges health;
        health.Id = cId;
        health.Values[FakeEngine::kHealth] = 0.f;
        party.Guest().GetDispatcher().trigger(health);
    });
    party.Step(party.Guest());
    auto& copy = guest.Actors.at(cCopy);
    copy.Dead = copy.BlockResurrect = true;
    NotifyDeathStateChange alive;
    alive.Id = cId;
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(alive); });
    CHECK(copy.ResurrectCalls == 1);
    CHECK(copy.Dead);
    for (int i = 0; i < 10; ++i)
        party.Step(party.Guest());
    CHECK(copy.ResurrectCalls == 1);

    party.As(party.Guest(), [&] { guest.FinishDeath(cCopy); });
    for (int i = 0; i < 3; ++i)
        party.Step(party.Guest());
    CHECK_FALSE(copy.Dead);
    CHECK(copy.ResurrectCalls == 2);
    CHECK(guest.GetActorValue(cCopy, FakeEngine::kHealth) == 100.f);
}

SCENARIO("Small hits are coalesced and flushed on the frame", "[values]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    uint32_t copy = 0;
    party.Until(
        [&] {
            for (const uint32_t cId : host.CopiesOf())
                if (host.Actors.at(cId).Player)
                    copy = cId;
            return copy != 0;
        },
        "the host has the guest's player");
    auto seen = std::make_shared<std::vector<RequestHealthChangeBroadcast>>();
    party.Host().GetHostService().GetNetwork()->SetFrontDoor([seen](const RequestMessage& acMessage, auto) {
        if (acMessage.GetOpcode() == RequestHealthChangeBroadcast::Opcode)
            seen->push_back(static_cast<const RequestHealthChangeBroadcast&>(acMessage));
        return true;
    });
    party.As(party.Host(), [&] {
        party.Host().GetDispatcher().trigger(HealthChangeEvent{copy, -0.25f});
        party.Host().GetDispatcher().trigger(HealthChangeEvent{copy, -0.5f});
    });
    CHECK(seen->empty());
    party.Step(party.Host());
    REQUIRE(seen->size() == 1);
    CHECK(seen->front().DeltaHealth == -0.75f);
    party.Step(party.Host());
    CHECK(seen->size() == 1);
}

SCENARIO("Resurrection before the delayed death action stays alive", "[death][values]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest has the guard");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    guest.HoldDeathActions = true;
    party.As(party.Host(), [&] {
        host.Hit(guard, 200.f);
        REQUIRE(host.IsActorDead(guard));
        host.ResurrectActor(guard);
        REQUIRE_FALSE(host.IsActorDead(guard));
    });
    guest.HoldDeathActions = false;
    party.Until([&] { return guest.Actors.at(cCopy).ResurrectCalls != 0 && !guest.IsActorDead(cCopy); },
                "the old death action cannot undo the owner's resurrection");
}

SCENARIO("A newer death action is not undone by an older alive state", "[death][values]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSmoke, {}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest has the guard");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    const uint32_t cId = SessionOf(party.Guest(), cCopy);
    party.As(party.Guest(), [&] {
        NotifyDeathStateChange alive;
        alive.Id = cId;
        alive.DeathActionTick = 123;
        party.Guest().GetDispatcher().trigger(alive);
        ActionEvent action;
        action.Tick = 124;
        guest.ReplayDeathAction(cCopy, action);
    });
    for (int i = 0; i < 3; ++i)
        party.Step(party.Guest());
    CHECK(guest.IsActorDead(cCopy));
    CHECK(guest.Actors.at(cCopy).ResurrectCalls == 0);
}

SCENARIO("A following player's copy applies the confirmed death after a refused native action", "[death][travel]")
{
    Party party(1, SmokeWorld);
    party.Agree("the players are present");
    auto& guest = Party::Engine(party.Guest());
    uint32_t copy = 0;
    for (const auto& [cId, cActor] : guest.Actors)
        if (cActor.Player && cActor.Copy)
            copy = cId;
    REQUIRE(copy);
    NotifyDeathStateChange death;
    death.Id = SessionOf(party.Guest(), copy);
    death.IsDead = true;
    death.DeathActionTick = 123;
    party.As(party.Guest(), [&] {
        party.Guest().GetDispatcher().trigger(death);
        party.Guest().GetDispatcher().trigger(DeathActionReplayedEvent{copy, 123});
    });
    CHECK(guest.IsActorDead(copy));
}
