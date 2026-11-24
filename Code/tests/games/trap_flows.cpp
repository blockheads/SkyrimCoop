// Traps (docs/design/traps.md): the host runs a trap's script; each game judges what touched its own player; the
// player's game takes the hit.
#include "Party.h"
#include "Fixtures.h"
#include <Games/ScriptMotion.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kWall = 0x00054B15;
constexpr uint32_t kDarts = 0x0003720F;
constexpr uint32_t kDart = 0x000236A5;
constexpr uint32_t kDartProjectile = 0x0003BE11; // a trap dart's projectile base
constexpr uint32_t kMarker = 0x00037210; // a trap that never hits: what reaches it, came after the rest
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint32_t kTripwire = 0x0007998B; // WarehouseTraps' (Tripwire: keyframed by its OnCellAttach)

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
    aEngine.References[kWall] = {.Cell = kSmoke, .TrapCanHit = true, .TrapDamage = 30.f};
    aEngine.References[kDarts] = {.Cell = kSmoke};
    aEngine.References[kMarker] = {.Cell = kSmoke};
    aEngine.References[kTripwire] = {.Cell = kSmoke};
}

float Health(FakeEngine& aEngine, uint32_t aActor = FakeEngine::kPlayer)
{
    return aEngine.GetActorValue(aActor, FakeEngine::kHealth);
}
// Every guest's earlier message reached the host, and the host's answers every guest.
void Settle(Party& aParty, size_t aGuests)
{
    FakeEngine& host = Party::Engine(aParty.Host());
    const size_t cBefore = host.References.at(kMarker).TrapContacts.size();
    for (size_t i = 0; i < aGuests; ++i)
        aParty.As(aParty.Guest(i), [&] { Party::Engine(aParty.Guest(i)).TouchesTrap(FakeEngine::kPlayer, kMarker); });
    aParty.Until([&] { return host.References.at(kMarker).TrapContacts.size() == cBefore + aGuests; }, "the guests' markers reach the host");
    aParty.As(aParty.Host(), [&] { host.ScriptPlaysAnimation(kMarker, "marker"); });
    for (size_t i = 0; i < aGuests; ++i)
        aParty.Until([&] { return !Party::Engine(aParty.Guest(i)).References.at(kMarker).GraphEvents.empty(); }, "the host's marker reaches a guest");
}
} // namespace

// Playtest 2026-11-22, items 13 and 14: a trap that touched the guest's player did nothing.
SCENARIO("A trap that touches a guest's player hurts it once, on its own game", "[traps]")
{
    Party party(2, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest(0));
    FakeEngine& other = Party::Engine(party.Guest(1));
    party.Until([&] { return PlayerCopy(host, 1) && PlayerCopy(guest, 1) && PlayerCopy(other, 1); }, "every game has every player");
    const float cGuest = Health(guest), cOther = Health(other), cHost = Health(host);

    WHEN("every game's engine sees the touch: the guest's own player, and its copies")
    {
        party.As(party.Guest(0), [&] { guest.TouchesTrap(FakeEngine::kPlayer, kWall); });
        party.As(party.Host(), [&] { host.TouchesTrap(PlayerCopy(host), kWall); });
        party.As(party.Guest(1), [&] { other.TouchesTrap(PlayerCopy(other), kWall); });
        party.Until([&] { return Health(guest) == cGuest - 30.f; }, "the guest's player takes the hit");
        Settle(party, 2);
        CHECK(Health(guest) == cGuest - 30.f);
        CHECK(host.References.at(kWall).TrapContacts == std::vector{PlayerCopy(host)});
        CHECK(Health(other) == cOther);
        CHECK(Health(host) == cHost);
    }
    WHEN("the host's script has the trap in CannotHit")
    {
        host.References.at(kWall).TrapCanHit = false;
        party.As(party.Guest(0), [&] { guest.TouchesTrap(FakeEngine::kPlayer, kWall); });
        party.Until([&] { return host.References.at(kWall).TrapContacts.size() == 1; }, "the host's script hears of it");
        Settle(party, 2);
        CHECK(Health(guest) == cGuest);
    }
}

SCENARIO("A trap that touches the host's player or an NPC hurts it on the host alone", "[traps]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && PlayerCopy(guest) && PlayerCopy(host); }, "each game has the other's actors");
    const float cHost = Health(host), cGuest = Health(guest);

    party.As(party.Host(), [&] { host.TouchesTrap(FakeEngine::kPlayer, kWall); });
    party.As(party.Guest(), [&] { guest.TouchesTrap(PlayerCopy(guest), kWall); });
    party.Until([&] { return Health(host) == cHost - 30.f; }, "the host's player takes the hit");
    party.Until([&] { return Health(guest, PlayerCopy(guest)) == Health(host); }, "its copy shows it");

    const float cNpc = Health(host, guard);
    party.As(party.Host(), [&] { host.TouchesTrap(guard, kWall); });
    party.Until([&] { return Health(host, guard) == cNpc - 30.f; }, "the NPC takes the hit");
    Settle(party, 1);
    CHECK(Health(guest) == cGuest);
    CHECK(Health(host) == cHost - 30.f);
}

// The spike wall: a guest's graph, still in its clip as the host's next event came, refused it and stayed behind.
SCENARIO("A trap's animations play on a guest in order, each once its clip has ended there", "[traps]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.ScriptPlaysAnimation(kWall, "180For"); });
    party.Until([&] { return guest.References.at(kWall).GraphEvents == std::vector<std::string>{"180For"}; }, "the guest's wall swings");
    guest.References.at(kWall).InClip = true;
    party.As(party.Host(), [&] {
        host.ScriptPlaysAnimation(kWall, "180Back");
        host.ScriptPlaysAnimation(kWall, "Rearm");
    });
    Settle(party, 1);
    CHECK(guest.References.at(kWall).GraphEvents.size() == 1);
    guest.References.at(kWall).InClip = false;
    party.Until([&] { return guest.References.at(kWall).GraphEvents == std::vector<std::string>{"180For", "180Back", "Rearm"}; },
                "the guest's wall goes back and arms");
}

// warehouse_traps_hurt_both (2026-11-24): the host's TRIGGER on the mace's tripwire waited for the guest's graph, but
// its destroyed flag right after it did not; the tripwire, destroyed first, never took TRIGGER and the mace never fell.
SCENARIO("A trap's change after its animation waits behind it on a guest", "[traps]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    guest.References.at(kWall).InClip = true;
    party.As(party.Host(), [&] {
        host.ScriptPlaysAnimation(kWall, "TRIGGER");
        host.ScriptSetsDestroyed(kWall, true);
    });
    Settle(party, 1);
    CHECK_FALSE(guest.References.at(kWall).Destroyed);
    guest.References.at(kWall).InClip = false;
    party.Until([&] { return guest.References.at(kWall).Destroyed; }, "the guest's tripwire breaks");
    CHECK(guest.References.at(kWall).GraphEvents == std::vector<std::string>{"TRIGGER"});
}

// WarehouseTraps' fire plate and poison gas (2026-11-23, 2026-11-24): a hazard's or explosion's effect on a guest's
// player is the host's verdict on its copy; the guest's own copies of them apply nothing. A hazard casts only on an
// actor not under its spell, which the copy now answers with its player's effects: one burn, not one a tick.
SCENARIO("A hazard's or explosion's effect on a guest's player is the host's verdict, once", "[traps]")
{
    static constexpr uint32_t kFireHazardSpell = 0x0005A1A5;
    constexpr uint32_t kFireDamage = 0x0005A1A6;
    static constexpr uint32_t kPoisonSpell = 0x000A0370;
    constexpr uint32_t kPoison = 0x0009F28F;
    Party party(1, [](FakeEngine& aEngine) {
        Plugins(aEngine);
        aEngine.SpellForms[kFireHazardSpell] = {};
        aEngine.SpellForms[kPoisonSpell] = {};
    });
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host has the guest's player");
    const auto cOn = [&](uint32_t aSpell) {
        return std::ranges::count_if(guest.Effects, [&](const auto& acEffect) { return acEffect.Target == FakeEngine::kPlayer && acEffect.Spell == aSpell; });
    };
    party.As(party.Guest(), [&] {
        guest.HazardHits(FakeEngine::kPlayer, kFireHazardSpell, kFireDamage, 5.f);
        guest.ExplosionHits(FakeEngine::kPlayer, 0xFF000900, kPoisonSpell, kPoison, 2.f);
    });
    // Two ticks before the copy's answer can come back (review of #129): both go out, one burn.
    party.As(party.Host(), [&] {
        host.HazardHits(PlayerCopy(host), kFireHazardSpell, kFireDamage, 5.f);
        host.HazardHits(PlayerCopy(host), kFireHazardSpell, kFireDamage, 5.f);
    });
    party.Until([&] { return cOn(kFireHazardSpell) == 1; }, "the host's burn reaches the guest");
    party.Until([&] { return host.ActorHasEffect(PlayerCopy(host), kFireDamage); }, "the host's copy holds its player's burn");
    party.As(party.Host(), [&] {
        for (int i = 0; i < 3; ++i)
            host.HazardHits(PlayerCopy(host), kFireHazardSpell, kFireDamage, 5.f);
        host.ExplosionHits(PlayerCopy(host), 0xFF000901, kPoisonSpell, kPoison, 2.f);
    });
    party.Until([&] { return cOn(kPoisonSpell) == 1; }, "the host's poison reaches the guest");
    Settle(party, 1);
    CHECK(cOn(kFireHazardSpell) == 1);
    CHECK(cOn(kPoisonSpell) == 1);
}

// WarehouseTraps' gas (TrapPoisonGas, 2026-11-24): its script places an explosion on whoever stands in it unless they
// already have its effect (HasMagicEffectWithKeyword). The guest's copy never had, so every trigger fired on it, and
// its poison stacked on the guest. A copy answers with its owner's effects.
SCENARIO("A script asking a guest's copy for an effect gets its player's answer", "[traps][effects]")
{
    static constexpr uint32_t kPoisonSpell = 0x000A0370;
    constexpr uint32_t kPoison = 0x0009F28F;
    Party party(1, [](FakeEngine& aEngine) {
        Plugins(aEngine);
        aEngine.SpellForms[kPoisonSpell] = {};
    });
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host has the guest's player");
    CHECK_FALSE(host.ActorHasEffect(PlayerCopy(host), kPoison));
    party.As(party.Host(), [&] { host.ExplosionHits(PlayerCopy(host), 0xFF000901, kPoisonSpell, kPoison, 2.f); });
    party.Until([&] { return host.ActorHasEffect(PlayerCopy(host), kPoison); }, "the host's copy has the guest's poison");
    CHECK(std::ranges::count_if(guest.Effects, [](const auto& acEffect) { return acEffect.Target == FakeEngine::kPlayer; }) >= 1);
}

SCENARIO("A dart an object fires flies on every other game, once, with no shooter", "[traps]")
{
    Party party(2, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest(0));
    FakeEngine& other = Party::Engine(party.Guest(1));
    party.Until([&] { return PlayerCopy(host, 1) != 0; }, "the host has both guests");
    WHEN("the host's trap fires")
    {
        party.As(party.Host(), [&] { host.ObjectShoots(kDarts, 0, kDartProjectile, kSmoke, {10.f, 20.f, 30.f}, false, kDart); });
        party.Until([&] { return guest.Shots.size() == 1 && other.Shots.size() == 1; }, "both guests fire it");
        Settle(party, 2);
        CHECK(guest.Shots.size() == 1);
        CHECK(host.Shots.size() == 1); // its own
        // Its hit on the guest's copy is the host's verdict, taken by the guest's player (darts_hurt_puller: the guest's
        // dart, relaunched with no shooter, had no damage, and the host's on the copy was dropped).
        const float cBefore = Health(guest, FakeEngine::kPlayer);
        party.As(party.Host(), [&] { host.ObjectProjectileHits(PlayerCopy(host, 0), 9.f); });
        party.Until([&] { return Health(guest, FakeEngine::kPlayer) <= cBefore - 9.f; }, "the guest's player takes the dart's hit");
        // Its poison, cast by the hit copy on itself (44205), runs on the guest's player with that player as its caster:
        // its conditions read the caster (darts_hurt_puller: sent with none, the guest's engine refused it).
        party.As(party.Host(), [&] {
            host.ObjectProjectileHits(PlayerCopy(host, 0), 0.8f, FakeEngine::kDartPoison, FakeEngine::kDartPoisonEffect, 3.f);
        });
        party.Until([&] { return guest.ActorHasEffect(FakeEngine::kPlayer, FakeEngine::kDartPoisonEffect); }, "the guest's player is poisoned");
        // The guest's own relaunch of an exploding shot (a catapult's) hits its player too: dropped, the host's counts.
        const float cAfter = Health(guest, FakeEngine::kPlayer);
        party.As(party.Guest(0), [&] { guest.ObjectProjectileHits(FakeEngine::kPlayer, 30.f); });
        Settle(party, 2);
        CHECK(Health(guest, FakeEngine::kPlayer) == Approx(cAfter));
    }
    WHEN("the host's flame jet streams a concentration spell (Morvunskar's, Flames)")
    {
        for (int i = 0; i < 3; ++i)
            party.As(party.Host(), [&] { host.ObjectShoots(kDarts, 0x00012FCD, 0x00012FCF, kSmoke, {10.f, 20.f, 30.f}); });
        party.Until([&] { return guest.Shots.size() == 3 && other.Shots.size() == 3; }, "every puff flies on both guests");
    }
    WHEN("a guest's engine fires one of its own (its player's Storm Call)")
    {
        party.As(party.Guest(0), [&] { guest.ObjectShoots(kDarts, 0, kDartProjectile, kSmoke, {10.f, 20.f, 30.f}, false, kDart); });
        party.Until([&] { return host.Shots.size() == 1 && other.Shots.size() == 1; }, "the host and the other guest fire it");
        CHECK(guest.Shots.size() == 1); // its own
    }
}

// A copy made after its owner's effect started (review of #129): the effect predates the join, and only a change would
// have sent it. The spawn carries the held effects, and the host's record keeps them for later joiners.
SCENARIO("A copy made after its owner's effect started answers with it", "[traps][effects]")
{
    static constexpr uint32_t kPoisonSpell = 0x000A0370;
    constexpr uint32_t kPoison = 0x0009F28F;
    Party party(2, [](FakeEngine& aEngine) {
        Plugins(aEngine);
        aEngine.SpellForms[kPoisonSpell] = {};
    }, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& first = Party::Engine(party.Guest(0));
    FakeEngine& second = Party::Engine(party.Guest(1));
    party.As(party.Host(), [&] { host.StartsEffect({FakeEngine::kPlayer, 0, kPoisonSpell, kPoison}); });

    party.Join(party.Guest(0));
    party.Until([&] { return PlayerCopy(first) != 0; }, "the first guest has the host's player");
    party.Until([&] { return first.ActorHasEffect(PlayerCopy(first), kPoison); }, "its copy of the host's player is poisoned");
    party.As(party.Guest(0), [&] { first.StartsEffect({FakeEngine::kPlayer, 0, kPoisonSpell, kPoison}); });
    party.Until([&] { return PlayerCopy(host) && host.ActorHasEffect(PlayerCopy(host), kPoison); }, "the host's copy of the first guest is poisoned");

    party.Join(party.Guest(1));
    party.Until([&] { return PlayerCopies(second).size() == 2; }, "the second guest has both other players");
    for (const uint32_t cCopy : PlayerCopies(second))
        CHECK(second.ActorHasEffect(cCopy, kPoison));
}

// WarehouseTraps' tripwire (2026-11-24): its script keyframes its rope as its cell attaches, on the host alone; a
// guest's stayed its model's dynamic, its player pushed it and took it. A script's motion type is the host's row.
SCENARIO("A script's motion type on the host's object reaches every guest, joined now or later", "[traps][motion]")
{
    Party party(2, Plugins, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& first = Party::Engine(party.Guest(0));
    FakeEngine& second = Party::Engine(party.Guest(1));
    constexpr uint32_t kKeyframed = 4, kDynamic = 1;
    party.As(party.Host(), [&] { host.ScriptSetsMotionType(kTripwire, kKeyframed); });

    party.Join(party.Guest(0));
    party.Until([&] { return first.References.at(kTripwire).Motion == ScriptMotion::Pack(kKeyframed, false); },
                "the first guest's tripwire is keyframed as it joins");
    party.As(party.Host(), [&] { host.ScriptSetsMotionType(kTripwire, kDynamic); }); // tripped: Tripwire's Active state
    party.Until([&] { return first.References.at(kTripwire).Motion == ScriptMotion::Pack(kDynamic, false); },
                "the first guest's tripwire goes dynamic with the host's");

    party.Join(party.Guest(1));
    party.Until([&] { return second.References.at(kTripwire).Motion == ScriptMotion::Pack(kDynamic, false); },
                "a later guest's tripwire is as the host's is now");
}
