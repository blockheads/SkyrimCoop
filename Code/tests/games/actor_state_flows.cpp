#include "Party.h"
#include "Fixtures.h"
#include <Games/Animation/ActorStateRules.h>
#include <Services/CopyRecords.h>
#include <SessionIds.h>
#include <Structs/ActorStatePart.h>

// Actor state as a record part (docs/design/one-simulator.md, piece 2): what an actor's owner set beside its graph
// reaches its copy whenever that copy appears.
namespace
{
constexpr uint32_t kCell = 0x31D2E;
constexpr uint32_t kGuard = 0xD8C56;
constexpr uint32_t kGuardBase = 0x3DEE0;
constexpr uint32_t kMerchant = 0x1A66B;
constexpr uint32_t kMerchantBase = 0x13BA1;
constexpr uint32_t kBleedout = 8u << 21;

// Both games have the two placed NPCs; aLate's reference is still loading on the guest.
auto TwoNpcs(uint32_t aLate)
{
    return [aLate](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        aEngine.PlaceNpc(kGuard, kGuardBase, kCell, {});
        aEngine.PlaceNpc(kMerchant, kMerchantBase, kCell, {});
        if (aLate && aEngine.Name != "Dovahkiin")
        {
            aEngine.Actors.at(aLate).Initializing = true;
            aEngine.Actors.at(aLate).Loaded = false;
        }
    };
}
// The actor state part aGame holds of the host's aFormId (its session id from the host).
ActorStatePart HeldPart(Party& aParty, World& aGame, uint32_t aFormId)
{
    uint32_t id = 0;
    aParty.As(aParty.Host(), [&] { id = Utils::GetSessionIdByFormId(aFormId).value_or(0); });
    ActorStatePart part;
    if (const RecordPart* cpPart = aGame.ctx().at<CopyRecords>().Find(id, RecordKind::kActorState))
        (void)FromPartBytes(cpPart->Bytes, part);
    return part;
}
} // namespace

SCENARIO("A copy made after its owner sneaked, blocked and went down stands so", "[actor-state][records]")
{
    // Sneak (37951) and block (39004, actions 45/47) are set by actions only, which a copy on its stream never plays;
    // bleedout by the owner's own code. The copy appears after all three.
    Party party(1, TwoNpcs(kGuard));
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.IsActorCopy(kMerchant); }, "the spawns are through");
    REQUIRE_FALSE(guest.IsActorCopy(kGuard));

    auto& owner = host.Actors.at(kGuard);
    owner.State1 |= ActorStateRules::kSneaking | kBleedout;
    owner.State2 |= ActorStateRules::kWantBlocking;
    party.Until([&] { return (HeldPart(party, party.Guest(), kGuard).Flags1 & ActorStateRules::kSneaking) != 0; },
                "the guest holds the sneaking part while the reference loads");
    party.As(party.Guest(), [&] { guest.FinishLoading(kGuard); });
    party.Until([&] { return guest.IsActorCopy(kGuard); }, "the reference becomes the copy");
    const auto& cCopy = guest.Actors.at(kGuard);
    party.Until([&] { return (cCopy.State1 & ActorStateRules::kSneaking) != 0; }, "the copy sneaks");
    CHECK((cCopy.State1 & ActorStateRules::kLifeMask) == kBleedout);
    CHECK((cCopy.State2 & ActorStateRules::kWantBlocking) != 0);

    owner.State1 &= ~(ActorStateRules::kSneaking | kBleedout);
    party.Until([&] { return (cCopy.State1 & (ActorStateRules::kSneaking | ActorStateRules::kLifeMask)) == 0; }, "the copy stands up");
}

SCENARIO("A copy whose owner drew with no action it could replay shows its owner's hands", "[actor-state][weapon-draw]")
{
    // The draw's result reaches the copy as state (was InventoryService's 0.25 s convergence poll).
    Party party(1, TwoNpcs(0));
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.IsActorCopy(kGuard); }, "the guest has the guard's copy");
    for (const bool cDraw : {true, false})
    {
        party.As(party.Host(), [&] { host.Draws(kGuard, cDraw); });
        party.Until([&] { return guest.GetWeaponState(kGuard) == (cDraw ? WeaponState::Drawn : WeaponState::Sheathed); },
                    cDraw ? "the copy's hands are up" : "the copy's hands are down");
    }
}

SCENARIO("A copy's kill move waits for its killer to appear", "[actor-state][kill-move][records]")
{
    // EnterKillMove (38613) records the victim's killer; on a copy it is entered from the record, with the killer's
    // copy, once that copy exists (R5).
    Party party(1, TwoNpcs(kMerchant));
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.IsActorCopy(kGuard); }, "the guest has the guard's copy");
    REQUIRE_FALSE(guest.IsActorCopy(kMerchant));

    host.Actors.at(kGuard).InKillMove = true;
    host.Actors.at(kGuard).Killer = kMerchant;
    party.Until([&] { return HeldPart(party, party.Guest(), kGuard).InKillMove; }, "the guest holds the kill move part");
    party.Frame();
    const auto& cCopy = guest.Actors.at(kGuard);
    CHECK_FALSE(cCopy.InKillMove); // its killer is not here

    party.As(party.Guest(), [&] { guest.FinishLoading(kMerchant); });
    party.Until([&] { return guest.IsActorCopy(kMerchant); }, "the killer's copy appears");
    party.Until([&] { return cCopy.InKillMove; }, "the guard's copy enters the kill move");
    CHECK(cCopy.Killer == kMerchant);

    // Its owner left the kill move and no pair plays here (no clip, no KillMoveEnd to come): out of it, alive.
    host.Actors.at(kGuard).InKillMove = false;
    party.Until([&] { return !cCopy.InKillMove; }, "the copy leaves the kill move its owner left");
    CHECK_FALSE(cCopy.Dead);
    CHECK(cCopy.Killer == 0);
}

SCENARIO("A copy in its pair's clip keeps its kill move until its own end, whatever its owner's part says", "[actor-state][kill-move]")
{
    Party party(1, TwoNpcs(0));
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.IsActorCopy(kGuard); }, "the guest has the guard's copy");
    host.Actors.at(kGuard).InKillMove = true;
    auto& copy = guest.Actors.at(kGuard);
    party.Until([&] { return copy.InKillMove; }, "the copy enters the kill move");
    copy.InPairClip = true;
    host.Actors.at(kGuard).InKillMove = false;
    party.Until([&] { return !HeldPart(party, party.Guest(), kGuard).InKillMove; }, "the guest holds the part out of it");
    for (int i = 0; i < 5; ++i)
        party.Frame();
    CHECK(copy.InKillMove); // its clip still plays
    copy.InPairClip = false;
    party.Until([&] { return !copy.InKillMove; }, "its clip over with no end event: out of it");
}

SCENARIO("A dying owner's dropped hands do not sheathe its copy before its death; a copy mid-draw is put right after", "[actor-state][weapon-draw]")
{
    Party party(1, TwoNpcs(0));
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.IsActorCopy(kGuard); }, "the guest has the guard's copy");
    party.As(party.Host(), [&] { host.Draws(kGuard, true); });
    party.Until([&] { return guest.GetWeaponState(kGuard) == WeaponState::Drawn; }, "the copy's hands are up");

    // The copy is mid-sheathe (an animation of its own) when its owner's drawn hands are declared again.
    party.As(party.Host(), [&] { host.Draws(kGuard, false); });
    party.Until([&] { return guest.GetWeaponState(kGuard) == WeaponState::Sheathed; }, "the copy's hands are down");
    guest.Actors.at(kGuard).Weapon = WeaponState::Drawing;
    party.As(party.Host(), [&] { host.Draws(kGuard, true); });
    party.Until([&] { return HeldPart(party, party.Guest(), kGuard).HandsUp; }, "the guest holds the drawn part");
    for (int i = 0; i < 5; ++i)
        party.Frame();
    CHECK(guest.GetWeaponState(kGuard) == WeaponState::Drawing); // not cut short
    guest.Actors.at(kGuard).Weapon = WeaponState::Sheathed;       // its animation ended wrong
    party.Until([&] { return guest.GetWeaponState(kGuard) == WeaponState::Drawn; }, "the copy is put right after its animation");

    // Its owner dies: its hands drop (the engine's), and are not declared.
    host.Actors.at(kGuard).State1 |= uint32_t(ActorStateRules::kLifeDying) << 21;
    host.Actors.at(kGuard).Weapon = WeaponState::Sheathed;
    for (int i = 0; i < 20; ++i)
        party.Frame();
    CHECK(HeldPart(party, party.Guest(), kGuard).HandsUp);
    CHECK(guest.GetWeaponState(kGuard) == WeaponState::Drawn);
}

SCENARIO("A faction its owner gives an actor later reaches its copy, and a copy made after", "[actor-state][factions][records]")
{
    // The engine raises nothing for a faction write (FactionEvents' hooks on 37686/37688/37677): a script's
    // SetFactionRank on the host's NPC is declared as its factions part.
    constexpr uint32_t cFaction = 0x5C84E;
    Party party(1, TwoNpcs(kMerchant));
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.IsActorCopy(kGuard); }, "the guest has the guard's copy");
    REQUIRE_FALSE(guest.IsActorCopy(kMerchant));

    party.As(party.Host(), [&] {
        host.SetsFaction(kGuard, cFaction, 2);
        host.SetsFaction(kMerchant, cFaction, 1);
    });
    party.Until([&] { return guest.Factions[kGuard].contains(cFaction); }, "the guard's copy joins the faction");
    CHECK(guest.Factions[kGuard][cFaction] == 2);

    party.As(party.Guest(), [&] { guest.FinishLoading(kMerchant); });
    party.Until([&] { return guest.IsActorCopy(kMerchant); }, "the merchant's copy appears");
    party.Until([&] { return guest.Factions[kMerchant].contains(cFaction); }, "the merchant's copy is in it too");

    party.As(party.Host(), [&] { host.SetsFaction(kGuard, cFaction, -1); });
    party.Until([&] { return !guest.Factions[kGuard].contains(cFaction); }, "the guard's copy leaves it");
}
