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
    THEN("the guest's player is in combat with it") CHECK(guest.CopiesFightingPlayer == 1);

    party.As(party.Host(), [&] { host.Fights(guard, 0); });
    party.Until([&] { return guest.GetActorAiFields(cCopy()).CombatState == 0 && guest.CopiesFightingPlayer == 0; },
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
