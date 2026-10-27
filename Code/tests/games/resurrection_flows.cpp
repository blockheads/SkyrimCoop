#include "Party.h"

#include <Games/DeathActions.h>
#include <Messages/Relayed.h>

namespace
{
constexpr uint32_t kSmoke = 0x31D2E;
constexpr uint32_t kGuard = 0x1A67C;

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

uint32_t SessionOf(World& aGame, uint32_t aFormId)
{
    for (auto&& [cEntity, form, copy] : aGame.view<FormIdComponent, RemoteComponent>().each())
        if (form.Id == aFormId)
            return copy.Id;
    return 0;
}

bool OwnerReady(World& aGame, uint32_t aFormId)
{
    for (auto&& [cEntity, form, local, values] :
         aGame.view<FormIdComponent, LocalComponent, ActorValuesComponent>().each())
        if (form.Id == aFormId)
            return true;
    return false;
}

struct ResetLog
{
    bool Sent = false;
    void Receive(const ActionEvent& acAction) { Sent |= acAction.EventName == "IdleForceDefaultState"; }
};

struct DeathLog
{
    uint32_t Id;
    std::vector<NotifyDeathStateChange> Messages;

    void Receive(const NotifyDeathStateChange& acMessage)
    {
        if (acMessage.Id == Id)
            Messages.push_back(acMessage);
    }

    bool Saw(bool aDead) const
    {
        return std::any_of(Messages.begin(), Messages.end(),
                           [=](const auto& acMessage) { return acMessage.IsDead == aDead; });
    }
};

void ResurrectOwner(bool aRefusedFirst, bool aCopyRefuses)
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && OwnerReady(party.Host(), guard); },
                "the guard's owner and copy are ready");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    DeathLog log{SessionOf(party.Guest(), cCopy), {}};
    REQUIRE(log.Id);
    entt::scoped_connection connection =
        party.Guest().GetDispatcher().sink<NotifyDeathStateChange>().connect<&DeathLog::Receive>(&log);
    guest.HoldDeathActions = true;
    ActionEvent death;
    party.As(party.Host(), [&] {
        host.Hit(guard, 200.f);
        REQUIRE(host.IsActorDead(guard));
        death = host.GetActorLatestAnimation(guard);
        REQUIRE(DeathActions::IsDying(death.State1));
        if (!aCopyRefuses)
        {
            host.Actors.at(guard).BlockResurrect = aRefusedFirst;
            host.ResurrectActor(guard);
            CHECK(host.IsActorDead(guard) == aRefusedFirst);
            CHECK(host.Actors.at(guard).ResurrectCalls == 1);
        }
    });
    if (aRefusedFirst || aCopyRefuses)
    {
        party.Until([&] { return log.Saw(true); }, "the owner's death reaches the guest while its action is held");
        CHECK_FALSE(log.Saw(false));
        CHECK_FALSE(guest.IsActorDead(cCopy));
        CHECK(log.Messages.back().DeathActionTick == death.Tick);
        guest.HoldDeathActions = false;
        party.Until([&] { return guest.IsActorDead(cCopy); }, "the copy dies by the held owner action");
        CHECK(host.IsActorDead(guard));
        CHECK(guest.Actors.at(cCopy).ResurrectCalls == 0);
        for (int i = 0; i < 3; ++i)
            party.Frame();
        CHECK_FALSE(log.Saw(false));
        party.As(party.Host(), [&] {
            host.Actors.at(guard).BlockResurrect = false;
            guest.Actors.at(cCopy).BlockResurrect = aCopyRefuses;
            host.ResurrectActor(guard);
        });
    }
    party.Until([&] { return log.Saw(false); }, "the confirmed owner resurrection reaches the guest");
    CHECK_FALSE(host.IsActorDead(guard));
    CHECK_FALSE(log.Messages.back().IsDead);
    CHECK(log.Messages.back().DeathActionTick == death.Tick);
    if (aCopyRefuses)
    {
        party.Until([&] { return guest.Actors.at(cCopy).ResurrectCalls != 0; },
                    "the copy's engine attempts resurrection");
        CHECK(guest.IsActorDead(cCopy));
        const auto cAttempts = guest.Actors.at(cCopy).ResurrectCalls;
        for (int i = 0; i < 3; ++i)
            party.Step(party.Guest());
        CHECK(guest.Actors.at(cCopy).ResurrectCalls == cAttempts);
        party.As(party.Guest(), [&] { guest.FinishDeath(cCopy); });
        party.Until([&] { return !guest.IsActorDead(cCopy); },
                    "the copy's final death event retries the refused resurrection");
        CHECK(guest.Actors.at(cCopy).ResurrectCalls == cAttempts + 1);
    }
    else if (!aRefusedFirst)
    {
        CHECK_FALSE(guest.IsActorDead(cCopy));
        CHECK(guest.Actors.at(cCopy).ResurrectCalls == 0);
        guest.HoldDeathActions = false;
        party.Until([&] { return guest.Actors.at(cCopy).ResurrectCalls != 0; },
                    "the delayed death action is corrected to alive");
    }
    party.Until([&] { return !guest.IsActorDead(cCopy); }, "the copy finishes alive as its owner does");
    const auto cAttempts = guest.Actors.at(cCopy).ResurrectCalls;
    party.As(party.Guest(), [&] { guest.ReplayDeathAction(cCopy, death); });
    for (int i = 0; i < 3; ++i)
        party.Step(party.Guest());
    CHECK_FALSE(host.IsActorDead(guard));
    CHECK_FALSE(guest.IsActorDead(cCopy));
    CHECK(guest.Actors.at(cCopy).ResurrectCalls == cAttempts + 1);
}
} // namespace

SCENARIO("An immediate owner resurrection survives the delayed death replay", "[death][write-events][resurrect-events]")
{
    ResurrectOwner(false, false);
}

SCENARIO("A refused owner resurrection stays dead until the engine accepts revival",
         "[death][write-events][resurrect-events]")
{
    ResurrectOwner(true, false);
}

SCENARIO("A copy's refused resurrection follows its owner's confirmed revival",
         "[death][write-events][resurrect-events]")
{
    ResurrectOwner(false, true);
}

SCENARIO("A resurrected NPC copy draws and attacks on its living graph", "[death][resurrect-animation]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && OwnerReady(party.Host(), guard); },
                "the guard and its copy are ready");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    party.As(party.Host(), [&] { host.Hit(guard, 200.f); });
    party.Until([&] { return guest.IsActorDead(cCopy); }, "the copy dies by the owner's action");
    REQUIRE_FALSE(guest.Actors.at(cCopy).GraphUpdating);
    ResetLog reset;
    entt::scoped_connection connection = party.Host().GetDispatcher().sink<ActionEvent>().connect<&ResetLog::Receive>(&reset);
    party.As(party.Host(), [&] { host.ResurrectActor(guard); });
    REQUIRE_FALSE(reset.Sent);
    party.Until([&] { return !guest.IsActorDead(cCopy); }, "the copy is resurrected");
    party.Until([&] { return guest.Actors.at(cCopy).GraphUpdating && guest.Actors.at(cCopy).GraphState != "dead"; },
                "the resurrected copy has a living graph");
    for (const char* cpEvent : {"WeapEquip", "attackStart"})
    {
        ActionEvent action;
        action.ActorId = guard;
        action.ActionId = 0x1338;
        action.EventName = cpEvent;
        action.Tick = party.Host().GetTransport().GetTick();
        party.As(party.Host(), [&] { party.Host().GetDispatcher().trigger(action); });
        party.Until([&] { return guest.GetActorLatestAnimation(cCopy).EventName == action.EventName; },
                    "the revived copy replays " + action.EventName);
    }
    CHECK(guest.CopiesOf(kGuard) == std::vector<uint32_t>{cCopy});
}
