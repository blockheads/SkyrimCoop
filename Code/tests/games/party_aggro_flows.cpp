#include "FakeCrime.h"
#include "Fixtures.h"
#include "Party.h"
#include <actorstate/AiStateChangeEvent.h>
#include <core/UpdateEvent.h>

namespace
{
void DamageSwitches(bool aGuestFirst)
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t enemy = 0;
    party.As(party.Host(), [&] { enemy = host.LoadNpc(0x23ABE, 0x31D2E, {100.f, 0.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(0x23ABE).size() == 1 && PlayerCopy(host); },
                "both games have the enemy and players");
    const uint32_t cCopy = guest.CopiesOf(0x23ABE).front();
    const uint32_t cGuest = PlayerCopy(host);
    const uint32_t cFirst = aGuestFirst ? cGuest : FakeEngine::kPlayer;
    const uint32_t cSecond = aGuestFirst ? FakeEngine::kPlayer : cGuest;
    party.As(party.Host(), [&] {
        host.CombatSelectionsPerFrame = 2;
        host.CombatProposals[enemy] = cFirst;
        host.CombatCandidates[enemy] = {{FakeEngine::kPlayer, 250.f, true}, {cGuest, 250.f, true}};
        host.CombatPerception[PartyThreat::State::Key(enemy, cSecond)] = {
            .Known = false, .Lost = true, .Detection = 0, .ClearPath = true};
        host.Actors.at(enemy).Ai.CombatState = 1;
        host.Actors.at(enemy).Ai.CombatTargetId = cFirst;
        party.Host().GetRunner().Trigger(AiStateChangeEvent{enemy});
    });
    party.Until([&] { return host.PartyThreats.Active; }, "the host's combat policy is active");
    CHECK_FALSE(guest.PartyThreats.Active);
    World& firstGame = aGuestFirst ? party.Guest() : party.Host();
    World& secondGame = aGuestFirst ? party.Host() : party.Guest();
    party.As(firstGame,
             [&] { Party::Engine(firstGame).Strikes(FakeEngine::kPlayer, aGuestFirst ? cCopy : enemy, 5.f); });
    party.Until([&] { return host.Actors.at(enemy).Values.ActorValuesList[24] < 96.f; },
                "the first player's damage is applied");
    party.As(secondGame,
             [&] { Party::Engine(secondGame).Strikes(FakeEngine::kPlayer, aGuestFirst ? enemy : cCopy, 30.f); });
    party.Until([&] { return host.Actors.at(enemy).Ai.CombatTargetId == cSecond; },
                "the enemy switches from its initial target to the party member doing more damage");
    const uint32_t cSecondOnGuest = aGuestFirst ? PlayerCopy(guest) : FakeEngine::kPlayer;
    party.Until([&] { return guest.GetActorAiFields(cCopy).CombatTargetId == cSecondOnGuest; },
                "the guest's gameplay AI state sees the same target switch");
    party.As(party.Host(), [&] { host.CombatProposals[enemy] = cFirst; });
    party.Frame();
    party.Frame();
    CHECK(host.Actors.at(enemy).Ai.CombatTargetId == cSecond);
    CHECK(guest.GetActorAiFields(cCopy).CombatTargetId == cSecondOnGuest);
}
} // namespace

SCENARIO("After the host dies its NPC fights the guest, whose copy runs no combat of its own", "[party-aggro][combat][death]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t enemy = 0;
    party.As(party.Host(), [&] { enemy = host.LoadNpc(0x23ABE, 0x31D2E, {100.f, 0.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(0x23ABE).size() == 1 && PlayerCopy(host); },
                "both games have the enemy and players");
    const uint32_t cEnemyCopy = guest.CopiesOf(0x23ABE).front();
    const uint32_t cGuest = PlayerCopy(host);
    party.Agree("both players are present");
    party.As(party.Host(), [&] {
        host.CombatCandidates[enemy] = {{FakeEngine::kPlayer, 100.f, true}, {cGuest, 250.f, true}};
        host.Fights(enemy, FakeEngine::kPlayer);
    });
    party.Until([&] { return host.PartyThreats.Active; }, "the host's combat policy is active");

    party.As(party.Host(), [&] { host.AttackedBy(cGuest, enemy); });
    CHECK(host.Actors.at(cGuest).OwnCombatTarget == 0);

    party.As(party.Host(), [&] { host.Hit(FakeEngine::kPlayer, 200.f); });
    party.Until([&] { return host.IsActorDead(FakeEngine::kPlayer); }, "the host's player dies");
    party.Until([&] { return host.Actors.at(enemy).Ai.CombatTargetId == cGuest; },
                "the NPC's native target moves from the dead host to the guest's copy");
    party.Until([&] { return guest.GetActorAiFields(cEnemyCopy).CombatTargetId == FakeEngine::kPlayer; },
                "the guest's game sees the NPC turn on its player");
    CHECK(host.Actors.at(cGuest).OwnCombatTarget == 0);
}

SCENARIO("A guest can draw aggro from the host with greater damage", "[party-aggro][combat]")
{
    DamageSwitches(false);
}
SCENARIO("The host can draw aggro from a guest with greater damage", "[party-aggro][combat]")
{
    DamageSwitches(true);
}

namespace
{
// Skyrim names the host's player in a guest's crime, so the NPC's fight starts on the far host (crime.md).
void CrimeFightGoesNearest(const CrimeRules::Grudge& acGrudge)
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    uint32_t witness = 0;
    party.As(party.Host(), [&] { witness = host.LoadNpc(0x23ABE, 0x31D2E, {100.f, 0.f, 0.f}); });
    party.Until([&] { return PlayerCopy(host); }, "the host has the guest's copy");
    const uint32_t cGuest = PlayerCopy(host);
    party.As(party.Host(), [&] {
        host.CombatProposals[witness] = FakeEngine::kPlayer;
        host.CombatCandidates[witness] = {{FakeEngine::kPlayer, 2000.f, true}, {cGuest, 150.f, true}};
        // Skyrim: the guest it sees is known at detection 100 with stealth points left (2026-11-05).
        host.CombatPerception[PartyThreat::State::Key(witness, cGuest)] = {.Known = true, .Detection = 100, .Stealth = 40};
        host.Actors.at(witness).Ai.CombatState = 1;
        host.Actors.at(witness).Ai.CombatTargetId = FakeEngine::kPlayer;
        party.Host().ctx().at<FakeCrime&>().Grudges[witness] = acGrudge;
        party.Host().GetRunner().Trigger(AiStateChangeEvent{witness});
    });
    party.Until([&] { return host.Actors.at(witness).Ai.CombatTargetId == cGuest; },
                "the NPC turns from the far host to the guest beside it");
}
} // namespace

SCENARIO("An NPC hostile for a party crime goes for the nearest player, not the one who did it", "[party-aggro][crime]")
{
    // The victim: the crime is on its kPlayerCrimeList.
    CrimeFightGoesNearest({.Victim = true});
}

SCENARIO("A theft's witness, only angry, goes for the nearest player", "[party-aggro][crime]")
{
    // ProcessPendingCrime (39631) / the owner's alarm (39651): kAngryWithPlayer, no faction flag, no crime against it.
    CrimeFightGoesNearest({.AngryWithPlayer = true});
}

SCENARIO("A guard answering a party crime goes for the nearest player", "[party-aggro][crime]")
{
    // AlertCrimeResponders (39695): StartCombat on the host's player, the guard only on the crime's witness list.
    CrimeFightGoesNearest({.Witness = true});
}

SCENARIO("A guard a script set on the guest turns on the host beside it", "[party-aggro][combat]")
{
    // Solitude's execution (guest-trips-platform-guard): stage 29 runs StartCombat(Game.GetPlayer()) for the guest's
    // trigger, so the guard's group holds the guest's copy only; to the guard's own reaction the host is a friend.
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(0x23ABE, 0x31D2E, {100.f, 0.f, 0.f}); });
    party.Until([&] { return PlayerCopy(host); }, "the host has the guest's copy");
    const uint32_t cGuest = PlayerCopy(host);
    party.As(party.Host(), [&] {
        host.CombatFriendly.insert(PartyThreat::State::Key(guard, FakeEngine::kPlayer));
        host.CombatProposals[guard] = cGuest;
        host.CombatCandidates[guard] = {{FakeEngine::kPlayer, 150.f, true}, {cGuest, 2000.f, true}};
        host.CombatPerception[PartyThreat::State::Key(guard, FakeEngine::kPlayer)] = {.Known = true, .Detection = 100, .Stealth = 40};
        host.Actors.at(guard).Ai.CombatState = 1;
        host.Actors.at(guard).Ai.CombatTargetId = cGuest;
        party.Host().GetRunner().Trigger(AiStateChangeEvent{guard});
    });
    party.Until([&] { return host.PartyThreats.IsPartyFoe(guard, FakeEngine::kPlayer); },
                "the guard fighting the guest is the whole party's foe");
    party.Until([&] { return !host.CombatFriendly.contains(PartyThreat::State::Key(guard, FakeEngine::kPlayer)); },
                "the host joins the guard's combat group");
    party.Until([&] { return host.Actors.at(guard).Ai.CombatTargetId == FakeEngine::kPlayer; },
                "the guard turns from the far guest to the host beside it");
}

SCENARIO("Party threat retires actors and players and stops with the host", "[party-aggro][combat]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t enemy = 0;
    party.As(party.Host(), [&] { enemy = host.LoadNpc(0x23ABE, 0x31D2E, {100.f, 0.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(0x23ABE).size() == 1 && PlayerCopy(host); }, "the fight is loaded");
    const uint32_t cGuest = PlayerCopy(host);
    const uint32_t cCopy = guest.CopiesOf(0x23ABE).front();
    party.As(party.Host(), [&] {
        host.Actors.at(enemy).Ai.CombatState = 1;
        host.Actors.at(enemy).Ai.CombatTargetId = FakeEngine::kPlayer;
    });
    party.As(party.Guest(), [&] { guest.Strikes(FakeEngine::kPlayer, cCopy, 5.f); });
    party.Until([&] { return host.PartyThreats.DamageOf(enemy, cGuest) > 0; }, "the host credits the guest's damage");
    CHECK_FALSE(guest.PartyThreats.Active);

    SECTION("Death clears the enemy's threat")
    {
        party.As(party.Host(), [&] { host.Actors.at(enemy).Dead = true; });
        party.Until([&] { return host.PartyThreats.DamageByPair.empty(); }, "the dead enemy is forgotten");
        CHECK(host.PartyThreats.Combats.empty());
    }
    SECTION("Combat ending clears the fight after the hit grace period")
    {
        party.As(party.Host(), [&] {
            host.Actors.at(enemy).Ai.CombatState = 0;
            party.Host().GetDispatcher().trigger(UpdateEvent{PartyThreat::kRecentHit + 0.1});
        });
        CHECK(host.PartyThreats.DamageByPair.empty());
        CHECK(host.PartyThreats.Combats.empty());
    }
    SECTION("A departing player loses their damage history")
    {
        party.Leave(party.Guest());
        party.Until([&] { return !host.PartyThreats.IsPlayer(cGuest); },
                    "the departed guest is removed from targeting");
        CHECK(host.PartyThreats.DamageByPair.empty());
    }
    SECTION("Stopping hosting clears the native selection policy")
    {
        party.As(party.Host(), [&] { party.Host().GetHostService().StopHosting(); });
        CHECK_FALSE(host.PartyThreats.Active);
        CHECK(host.PartyThreats.Players.empty());
        CHECK(host.PartyThreats.DamageByPair.empty());
    }
}
