// Magic across games (MagicService): a concentration spell's cast replays on the caster's copies (other spells
// travel as projectiles, CombatService); a spell on another game's actor is applied by that actor's owner.
#include "Party.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint32_t kFlames = 0x00012FCD;
constexpr uint32_t kFlamesEffect = 0x00013CA9; // FireDamageConcAimed

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

SCENARIO("A guest's Flames plays on its copy on the host's game", "[magic]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");

    party.As(party.Guest(), [&] { guest.CastsSpell(FakeEngine::kPlayer, kFlames, 1); });
    party.Until(
        [&] {
            return std::ranges::any_of(host.Casts, [&](const auto& acCast) {
                return acCast.Caster == PlayerCopy(host) && acCast.Spell == kFlames && acCast.Source == 1;
            });
        },
        "the host's copy of the guest casts Flames from the same hand");
    THEN("the guest's own cast is not sent back to it") CHECK(guest.Casts.size() == 1);
}

SCENARIO("A guest's spell on a host NPC is the host's to apply", "[magic]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && PlayerCopy(host) != 0; },
                "each game has the other's actors");

    party.As(party.Guest(),
             [&] { guest.SpellHits(guest.CopiesOf(kGuard)[0], FakeEngine::kPlayer, kFlames, kFlamesEffect, 10.f); });
    party.Until(
        [&] {
            return std::ranges::any_of(host.Effects, [&](const auto& acEffect) {
                return acEffect.Target == guard && acEffect.Caster == PlayerCopy(host) && acEffect.Spell == kFlames;
            });
        },
        "the host's game burns its guard, cast by its copy of the guest");
    THEN("the guard turns on the guest, and the guest's game applies nothing itself")
    {
        CHECK(std::ranges::count(host.Reactions, std::pair{guard, PlayerCopy(host)}) == 1);
        CHECK(guest.Effects.empty());
    }
}

SCENARIO("A host NPC's burning shows on its copy", "[magic]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest's game has a copy of the guard");
    // After its copy's spawn: the host's game sends an actor's effects once it owns it (SendOwned on a change).
    party.Agree("the games agree on the guard");

    party.As(party.Host(), [&] { host.Burns(guard, kFlamesEffect, true); });
    party.Until([&] { return guest.IsPlayingEffectHitVisuals(guest.CopiesOf(kGuard)[0], kFlamesEffect); },
                "the guard's copy burns");
    party.As(party.Host(), [&] { host.Burns(guard, kFlamesEffect, false); });
    party.Until([&] { return !guest.IsPlayingEffectHitVisuals(guest.CopiesOf(kGuard)[0], kFlamesEffect); },
                "its burning ends with the guard's");
}

SCENARIO("A host NPC fighting the guest's player fights it on the guest's game too", "[ai]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && PlayerCopy(host) != 0; },
                "each game has the other's actors");
    party.Agree("the games agree on the guard");

    party.As(party.Host(), [&] { host.Fights(guard, PlayerCopy(host)); });
    const auto cCopy = [&] { return guest.CopiesOf(kGuard)[0]; };
    party.Until([&] { return guest.GetActorAiFields(cCopy()).CombatTargetId == FakeEngine::kPlayer; },
                "the guard's copy fights the guest's own player");
    THEN("the guest's player is in combat with it") CHECK(guest.CopiesFightingPlayer == 1);

    party.As(party.Host(), [&] { host.Fights(guard, 0); });
    party.Until([&] { return guest.GetActorAiFields(cCopy()).CombatState == 0 && guest.CopiesFightingPlayer == 0; },
                "the fight ends on both");
}

SCENARIO("An instant FX Persist effect gone before the frame still flashes on copies", "[magic][instant-visuals]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    constexpr uint32_t cCure = 0xFBFF5;
    host.EffectVisualFlags[cCure] = guest.EffectVisualFlags[cCure] = HitVisualsRules::kPersist;
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guard has a copy");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    party.As(party.Host(), [&] { host.StartsHitVisuals(guard, cCure, 0.f, 1); });
    REQUIRE(host.GetActorActiveEffects(guard).empty());
    party.Until([&] { return !guest.FlashedVisuals.empty(); }, "the copy plays the instant effect once");
    CHECK(guest.FlashedVisuals == std::vector<std::pair<uint32_t, uint32_t>>{{cCopy, cCure}});
    for (int i = 0; i < 5; ++i) party.Frame();
    CHECK(guest.FlashedVisuals.size() == 1);
    CHECK_FALSE(guest.IsPlayingEffectHitVisuals(cCopy, cCure));
}
