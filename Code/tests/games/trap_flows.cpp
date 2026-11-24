// Traps (docs/design/traps.md): the host runs a trap's script; each game judges what touched its own player; the
// player's game takes the hit.
#include "Party.h"
#include "Fixtures.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kWall = 0x00054B15;
constexpr uint32_t kDarts = 0x0003720F;
constexpr uint32_t kDart = 0x000236A5;
constexpr uint32_t kMarker = 0x00037210; // a trap that never hits: what reaches it, came after the rest
constexpr uint32_t kGuard = 0x0001A67C;

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
    aEngine.References[kWall] = {.Cell = kSmoke, .TrapCanHit = true, .TrapDamage = 30.f};
    aEngine.References[kDarts] = {.Cell = kSmoke};
    aEngine.References[kMarker] = {.Cell = kSmoke};
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

// WarehouseTraps' fire plate and poison gas (2026-11-23): the host's hazard and explosion cast on its copy of the guest
// (no caster at all) while the guest's own copies of them cast on its player; both applied, the burns and poisons
// stacked. A cast with no caster is its target's own game's: the host's on the copy is not sent.
SCENARIO("A hazard's or explosion's effect on a guest's player is its own game's", "[traps]")
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
    party.As(party.Host(), [&] {
        for (int i = 0; i < 4; ++i)
            host.SpellHits(PlayerCopy(host), 0, kFireHazardSpell, kFireDamage, 5.f);
        host.ExplosionHits(PlayerCopy(host), 0xFF000901, kPoisonSpell, kPoison, 2.f);
    });
    Settle(party, 1);
    CHECK(cOn(kFireHazardSpell) == 0);
    CHECK(cOn(kPoisonSpell) == 0);
    party.As(party.Guest(), [&] {
        guest.SpellHits(FakeEngine::kPlayer, 0, kFireHazardSpell, kFireDamage, 5.f);
        guest.ExplosionHits(FakeEngine::kPlayer, 0xFF000900, kPoisonSpell, kPoison, 2.f);
    });
    Settle(party, 1);
    CHECK(cOn(kFireHazardSpell) == 1);
    CHECK(cOn(kPoisonSpell) == 1);
}

// WarehouseTraps' gas (TrapPoisonGas, 2026-11-24): its script places an explosion on whoever stands in it unless they
// already have its effect (HasMagicEffectWithKeyword). The guest's copy never had: every trigger fired on it, and its
// poison stacked on the guest. A copy answers with its owner's effects.
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
    party.As(party.Guest(), [&] { guest.ExplosionHits(FakeEngine::kPlayer, 0xFF000900, kPoisonSpell, kPoison, 2.f); });
    party.Until([&] { return host.ActorHasEffect(PlayerCopy(host), kPoison); }, "the host's copy has the guest's poison");
}

SCENARIO("A dart an object of the host's world fires flies on every guest, once", "[traps]")
{
    Party party(2, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest(0));
    FakeEngine& other = Party::Engine(party.Guest(1));
    party.Until([&] { return PlayerCopy(host, 1) != 0; }, "the host has both guests");
    WHEN("the host's trap fires")
    {
        party.As(party.Host(), [&] { host.ObjectFires(kDarts, kDart, {10.f, 20.f, 30.f}); });
        party.Until([&] { return guest.Shots.size() == 1 && other.Shots.size() == 1; }, "both guests fire it");
        Settle(party, 2);
        CHECK(guest.Shots.size() == 1);
        CHECK(host.Shots.empty());
    }
    WHEN("a guest's engine fires one of its own")
    {
        party.As(party.Guest(0), [&] { guest.ObjectFires(kDarts, kDart, {10.f, 20.f, 30.f}); });
        Settle(party, 2);
        CHECK(host.Shots.empty());
        CHECK(other.Shots.empty());
    }
}
