// Magic across games (MagicService): a concentration spell's cast replays on the caster's copies (other spells
// travel as projectiles, CombatService); a spell on another game's actor is applied by that actor's owner.
#include "Party.h"
#include "Fixtures.h"

#include <SessionIds.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint32_t kFlames = 0x00012FCD;
constexpr uint32_t kFlamesEffect = 0x00013CA9; // FireDamageConcAimed
constexpr uint32_t kInvisibility = 0x00027EB6;

} // namespace

SCENARIO("A guest's Flames plays on its copy on the host's game", "[magic]")
{
    Party party(1, SmokeWorld);
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

SCENARIO("A guest joining while another channels Flames sees its copy cast it; the stop ends it", "[magic][join]")
{
    Party party(2, SmokeWorld, false);
    party.Join(party.Guest());
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& caster = Party::Engine(party.Guest());
    FakeEngine& late = Party::Engine(party.Guest(1));
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");

    party.As(party.Guest(), [&] { caster.CastsSpell(FakeEngine::kPlayer, kFlames, 1); });
    party.Until([&] { return host.Channels.contains({PlayerCopy(host), 1}); }, "the host's copy channels Flames");

    const auto cCopyChannels = [&] {
        return std::ranges::any_of(late.Channels, [&](const auto& acChannel) {
            return late.IsActorCopy(acChannel.first.first) && acChannel.first.second == 1 && acChannel.second == kFlames;
        });
    };
    party.Join(party.Guest(1));
    party.Until(cCopyChannels, "the late guest's copy of the caster channels Flames from the same hand");

    party.As(party.Guest(), [&] { caster.StopsCasting(FakeEngine::kPlayer, 1); });
    party.Until([&] { return host.Channels.empty() && late.Channels.empty(); }, "every copy stops with it");
    THEN("the caster's own channel is not sent back to it") CHECK(std::ranges::count(caster.Casts, kFlames, &FakeEngine::Cast::Spell) == 1);
}

SCENARIO("A one-shot cast plays once on each copy, every time, and a guest joining after gets none", "[magic][join]")
{
    Party party(2, SmokeWorld, false);
    party.Join(party.Guest());
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& caster = Party::Engine(party.Guest());
    FakeEngine& late = Party::Engine(party.Guest(1));
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");
    const auto cCopyCasts = [&](FakeEngine& aEngine) {
        return std::ranges::count_if(aEngine.Casts, [&](const auto& acCast) {
            return aEngine.IsActorCopy(acCast.Caster) && acCast.Spell == kInvisibility;
        });
    };

    party.As(party.Guest(), [&] { caster.CastsSpell(FakeEngine::kPlayer, kInvisibility, 0); });
    party.Until([&] { return cCopyCasts(host) == 1; }, "the host's copy casts it");
    party.As(party.Guest(), [&] { caster.CastsSpell(FakeEngine::kPlayer, kInvisibility, 0); });
    party.Until([&] { return cCopyCasts(host) == 2; }, "cast again, the host's copy casts it again");

    party.Join(party.Guest(1));
    party.Until([&] { return PlayerCopy(late, 0) && PlayerCopy(late, 1); }, "the late guest has both players' copies");
    for (int i = 0; i < 30; ++i)
        party.Frame();
    THEN("its copy casts nothing that ended long ago") CHECK(cCopyCasts(late) == 0);
}

SCENARIO("A host NPC channelling at a guest: a guest joining after casts it on its copy of that guest", "[magic][join]")
{
    Party party(2, SmokeWorld, false);
    party.Join(party.Guest());
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& late = Party::Engine(party.Guest(1));
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");
    party.As(party.Host(), [&] { host.CastsSpell(guard, kFlames, 1, PlayerCopy(host)); });

    party.Join(party.Guest(1));
    party.Until([&] { return late.CopiesOf(kGuard).size() == 1; }, "the late guest has the guard");
    party.Until([&] { return late.Channels.contains({late.CopiesOf(kGuard)[0], 1}); }, "its copy of the guard channels Flames");
    const auto cCast = std::ranges::find(late.Casts, late.CopiesOf(kGuard)[0], &FakeEngine::Cast::Caster);
    REQUIRE(cCast != late.Casts.end());
    THEN("at its copy of the first guest's player")
    {
        CHECK(cCast->Target != 0);
        CHECK(late.IsActorCopy(cCast->Target));
        CHECK(late.Actors.at(cCast->Target).Base != kGuard);
    }
}

// The actors of aBase a game holds: its own (not copies) and its copies.
std::pair<std::vector<uint32_t>, std::vector<uint32_t>> ActorsOf(const FakeEngine& acEngine, uint32_t aBase)
{
    std::pair<std::vector<uint32_t>, std::vector<uint32_t>> actors;
    for (const auto& [cId, cActor] : acEngine.Actors)
        if (cActor.Base == aBase)
            (cActor.Copy ? actors.second : actors.first).push_back(cId);
    return actors;
}

SCENARIO("A guest's familiar is one actor on every game", "[magic][summon]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");

    const auto cCast = [&] {
        party.As(party.Guest(), [&] {
            guest.SpellHits(FakeEngine::kPlayer, FakeEngine::kPlayer, FakeEngine::kConjureFamiliar, FakeEngine::kFamiliarEffect, 60.f);
        });
    };
    cCast();
    party.Until([&] { return ActorsOf(host, FakeEngine::kFamiliar).first.size() == 1 && guest.CopiesOf(FakeEngine::kFamiliar).size() == 1; },
                "the host's game made the familiar and the guest's shows its copy");
    THEN("the host's familiar is commanded by the guest's copy, its copy by the guest's player, and the guest made none")
    {
        CHECK(std::ranges::count_if(guest.Effects, [](const auto& acEffect) { return acEffect.Spell == FakeEngine::kConjureFamiliar; }) == 1);
        CHECK(host.Actors.at(ActorsOf(host, FakeEngine::kFamiliar).first[0]).Commander == PlayerCopy(host));
        CHECK(guest.Actors.at(guest.CopiesOf(FakeEngine::kFamiliar)[0]).Commander == FakeEngine::kPlayer);
        CHECK(ActorsOf(guest, FakeEngine::kFamiliar).first.empty());
    }

    WHEN("the guest casts it again")
    {
        cCast();
        party.Until([&] { return ActorsOf(host, FakeEngine::kFamiliar).first.size() == 1 && guest.CopiesOf(FakeEngine::kFamiliar).size() == 1 &&
                                 host.Effects.size() == 2; },
                    "the host's game dismissed the first for the second");
        THEN("still one familiar on each game") CHECK(ActorsOf(guest, FakeEngine::kFamiliar).first.empty());
    }
}

SCENARIO("A guest's spell on a host NPC is the host's to apply", "[magic]")
{
    Party party(1, SmokeWorld);
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
    Party party(1, SmokeWorld);
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

SCENARIO("A guest joining after another's familiar sees it commanded by that guest's character", "[magic][summon]")
{
    // PR #88's review: a late joiner hears the host's actors (the familiar) before the other guest's character.
    Party party(2, SmokeWorld, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& first = Party::Engine(party.Guest(0));
    FakeEngine& late = Party::Engine(party.Guest(1));
    party.Join(party.Guest(0));
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the first guest's player");
    party.As(party.Guest(0), [&] {
        first.SpellHits(FakeEngine::kPlayer, FakeEngine::kPlayer, FakeEngine::kConjureFamiliar, FakeEngine::kFamiliarEffect, 60.f);
    });
    party.Until([&] { return ActorsOf(host, FakeEngine::kFamiliar).first.size() == 1; }, "the host's game made the familiar");
    party.Join(party.Guest(1));
    party.Until([&] { return late.CopiesOf(FakeEngine::kFamiliar).size() == 1 && PlayerCopy(late) != 0; },
                "the late guest has the familiar and the other players");
    // The first guest's character, by its session id, on the late guest's game.
    uint32_t firstSession = 0;
    party.As(party.Host(), [&] { firstSession = Utils::GetSessionIdByFormId(PlayerCopy(host)).value_or(0); });
    REQUIRE(firstSession != 0);
    const uint32_t cFamiliar = late.CopiesOf(FakeEngine::kFamiliar)[0];
    uint32_t firstHere = 0;
    party.Until([&] {
        party.As(party.Guest(1), [&] { firstHere = Utils::GetFormIdBySessionId(firstSession); });
        return firstHere != 0 && late.Actors.at(cFamiliar).Commander == firstHere;
    }, "the familiar commanded by the first guest's character there");
}

SCENARIO("A host NPC already burning when its copy is made burns on the copy once it has 3D", "[magic][summon]")
{
    Party party(1, SmokeWorld, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] {
        guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f});
        host.Burns(guard, kFlamesEffect, true); // a summon's own shader, as the familiar's (summons.md)
    });
    // Skyrim (the familiar, 2026-11-21): the copy's 3D comes after its spawn, with no TESObjectLoadedEvent.
    guest.MadeActors3D = false;
    party.Join(party.Guest());
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest's game has a copy of the guard");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    for (int i = 0; i < 10; ++i)
        party.Frame(); // its visuals asked for, with no 3D to play on
    CHECK_FALSE(guest.IsPlayingEffectHitVisuals(cCopy, kFlamesEffect));
    guest.Actors.at(cCopy).Loaded = true;
    party.Until([&] { return guest.IsPlayingEffectHitVisuals(cCopy, kFlamesEffect); }, "the copy burns once its 3D is there");
}

SCENARIO("A host NPC fighting the guest's player fights it on the guest's game too", "[ai]")
{
    Party party(1, SmokeWorld);
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
    THEN("the guest's player is in combat with it") CHECK(guest.PlayerFlaggedInCombat());

    party.As(party.Host(), [&] { host.Fights(guard, 0); });
    party.Until([&] { return guest.GetActorAiFields(cCopy()).CombatState == 0 && !guest.PlayerFlaggedInCombat(); },
                "the fight ends on both");
}

SCENARIO("An instant FX Persist effect gone before the frame still flashes on copies", "[magic][instant-visuals]")
{
    Party party(1, SmokeWorld);
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

// Playtest 2026-11-22 item 12: a Bleak Falls Barrow trap's Rattles left the guest with no stamina regeneration (both
// Survival Mode variants applied, StaminaRate 5 - 2.5 - 2.5). Each game's player gets the one its conditions pick.
SCENARIO("A trap's disease on the guest's character applies only the effects its conditions pick, as on the host's player",
         "[magic][stamina]")
{
    const float cSurvival = GENERATE(0.f, 1.f);
    const uint32_t cPicked = cSurvival != 0.f ? FakeEngine::kSurvivalRattlesEffect : FakeEngine::kRattlesEffect;
    const uint32_t cOther = cSurvival != 0.f ? FakeEngine::kRattlesEffect : FakeEngine::kSurvivalRattlesEffect;
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");
    // Survival Mode's toggle, a global the guest's game holds as the host's.
    host.Globals[FakeEngine::kSurvivalModeEnabled] = cSurvival;
    guest.Globals[FakeEngine::kSurvivalModeEnabled] = cSurvival;

    // The trap (no actor) casts every effect of the spell, the picked one last; the engine's add check picks.
    party.As(party.Host(), [&] {
        for (const uint32_t cTarget : {PlayerCopy(host), FakeEngine::kPlayer})
            for (const uint32_t cEffect : {cOther, cPicked})
                host.SpellHits(cTarget, 0, FakeEngine::kRattles, cEffect, 2.5f);
    });
    const auto cHas = [](const FakeEngine& acEngine, uint32_t aEffect) {
        return std::ranges::any_of(acEngine.Effects, [&](const auto& acEffect) {
            return acEffect.Target == FakeEngine::kPlayer && acEffect.EffectId == aEffect;
        });
    };
    party.Until([&] { return cHas(guest, cPicked); }, "the guest's player catches Rattles");

    THEN("the host's player has the picked effect only")
    {
        CHECK(cHas(host, cPicked));
        CHECK_FALSE(cHas(host, cOther));
    }
    THEN("so has the guest's: its stamina still regenerates") CHECK_FALSE(cHas(guest, cOther));
}

SCENARIO("A relayed effect whose conditions ask for a dual cast lands on a dual cast only", "[magic][stamina]")
{
    constexpr uint32_t kOakflesh = 0x0005AD5C;
    const bool cDual = GENERATE(false, true);
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");

    // The host's player's spell on the guest's copy: its game decides the hit, the guest's checks the conditions.
    party.As(party.Host(), [&] {
        host.SpellHits(PlayerCopy(host), FakeEngine::kPlayer, kOakflesh, FakeEngine::kDualCastEffect, 10.f, cDual);
        host.SpellHits(PlayerCopy(host), FakeEngine::kPlayer, kOakflesh, FakeEngine::kRattlesEffect, 10.f, cDual);
    });
    const auto cHas = [&](uint32_t aEffect) {
        return std::ranges::any_of(guest.Effects, [&](const auto& acEffect) {
            return acEffect.Target == FakeEngine::kPlayer && acEffect.EffectId == aEffect;
        });
    };
    party.Until([&] { return cHas(FakeEngine::kRattlesEffect); }, "the guest's player gets the spell's plain effect");
    THEN("the dual-cast effect lands as the cast was") CHECK(cHas(FakeEngine::kDualCastEffect) == cDual);
}
