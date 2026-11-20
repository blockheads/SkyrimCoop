#include <catch2/catch.hpp>
#include <Games/Animation/ActionClaims.h>
#include <Messages/Payloads.h>
#include <Games/Animation/ReplayState.h>

TEST_CASE("A replayed pair on this game's player goes by the host's answer to its claim", "[copies][replay-state][kill-move]")
{
    using namespace ReplayState;
    // A kill move waits for the host's answer; refused, it is never played.
    CHECK(SettlePairedReplay(false, ClaimAnswer::kUnknown) == PairOutcome::kWait);
    CHECK(SettlePairedReplay(true, ClaimAnswer::kUnknown) == PairOutcome::kWait);
    CHECK(SettlePairedReplay(false, ClaimAnswer::kRefused) == PairOutcome::kSkip);
    CHECK(SettlePairedReplay(true, ClaimAnswer::kRefused) == PairOutcome::kSkip);
    // Claimed: played on a free player; one in its own kill move lost the claim, and yields.
    CHECK(SettlePairedReplay(false, ClaimAnswer::kClaimed) == PairOutcome::kPlay);
    CHECK(SettlePairedReplay(true, ClaimAnswer::kClaimed) == PairOutcome::kYield);
    // No kill move (a pair's other half coming back, a mount's pair): played unless the player is in a pair.
    CHECK(SettlePairedReplay(false, ClaimAnswer::kNotNeeded) == PairOutcome::kPlay);
    CHECK(SettlePairedReplay(true, ClaimAnswer::kNotNeeded) == PairOutcome::kSkip);
}

TEST_CASE("A kill move replayed on an NPC of this game's is never paired here", "[copies][replay-state][kill-move]")
{
    using namespace ReplayState;
    CHECK(ClaimedAway(true, false, false));
    CHECK_FALSE(ClaimedAway(true, false, true));  // this game's player: its game plays its half
    CHECK_FALSE(ClaimedAway(true, true, false));  // a copy already
    CHECK_FALSE(ClaimedAway(false, false, false)); // a mount's pair
    CHECK(PairsHere(false, false, true, true));
    CHECK_FALSE(PairsHere(false, false, false, true));
    CHECK(PairsHere(false, false, false, false));
    CHECK_FALSE(PairsHere(false, true, false, false));
    CHECK_FALSE(PairsHere(true, false, false, false));
    // A lent NPC, a copy here with no stream to replay: its pair is made here, the attacker's copy off its stream.
    CHECK(TakenVictim(true, ClaimAnswer::kClaimed, true, false));
    CHECK_FALSE(TakenVictim(true, ClaimAnswer::kUnknown, true, false));
    CHECK_FALSE(TakenVictim(true, ClaimAnswer::kClaimed, true, true)); // a player's copy: its own game plays its half
    CHECK(PairsHere(true, false, false, true, true));
    CHECK(PairOnStream(true));
    CHECK_FALSE(PairOnStream(true, true));
    CHECK_FALSE(PairOnStream(false));
}

TEST_CASE("A claimed kill move is done when its taken victim was killed or left it, a player partner's with the claimer's own kill move", "[copies][replay-state][kill-move]")
{
    using namespace ReplayState;
    CHECK_FALSE(PairDone(true, false, true, true));
    CHECK(PairDone(true, true, true, true));   // its clip's KillActor
    CHECK(PairDone(true, false, false, true)); // it left the pair alive
    CHECK_FALSE(PairDone(false, false, false, true));
    CHECK(PairDone(false, false, true, false));
}

TEST_CASE("A blocked mark refuses an owner's pair, never a replayed one", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::PairMarkClear(true, false));
    CHECK_FALSE(ReplayState::PairMarkClear(false, false));
    CHECK(ReplayState::PairMarkClear(false, true));
}

TEST_CASE("A copy in a pair takes no plain action of its owner", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::HeldByPair(true, false));
    CHECK_FALSE(ReplayState::HeldByPair(true, true));
    CHECK_FALSE(ReplayState::HeldByPair(false, false));
}

TEST_CASE("A yielded clip's blow is dropped until its player attacks anew", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::CancelledBlow(true));
    CHECK_FALSE(ReplayState::CancelledBlow(false));
    CHECK(ReplayState::StartsAttack("attackStart"));
    CHECK(ReplayState::StartsAttack("attackPowerStartLeft"));
    CHECK(ReplayState::StartsAttack("pa_KillMoveI"));
    CHECK_FALSE(ReplayState::StartsAttack("KillMoveI")); // the victim's half
    CHECK_FALSE(ReplayState::StartsAttack("moveStop"));
}

TEST_CASE("pairedStop is sent again only while the cancelled clip plays", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::ResendPairStop(1, false, true));
    CHECK_FALSE(ReplayState::ResendPairStop(0, false, true));
    CHECK_FALSE(ReplayState::ResendPairStop(1, true, true)); // a new kill move
    CHECK_FALSE(ReplayState::ResendPairStop(1, false, false));
}

TEST_CASE("Contact kills a kill move's victim only when both halves are this game's", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::ContactKills(false));
    CHECK_FALSE(ReplayState::ContactKills(true));
}

TEST_CASE("A copy holds its pose until its owner's death, or its time", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::PoseHeld(100, 200, false));
    CHECK_FALSE(ReplayState::PoseHeld(200, 200, false));
    CHECK_FALSE(ReplayState::PoseHeld(100, 200, true));
    CHECK_FALSE(ReplayState::PoseHeld(100, 0, false));
}

TEST_CASE("A copy getting off its mount is on its own body until its sit state leaves wants-to-stand", "[replay][graph-stream]")
{
    // Nothing else marks it: a copy's exit sets no GETTING_ON_OFF_MOUNT (Actor::Dismount 37906 never runs for it).
    CHECK(ReplayState::CopyOwnBody(false, false, false, ReplayState::kSitWantsToStand));
    CHECK_FALSE(ReplayState::CopyOwnBody(false, false, false, 0));
    CHECK_FALSE(ReplayState::CopyOwnBody(false, false, false, 3)); // sitting: riding, on its stream
    CHECK(ReplayState::CopyOwnBody(false, false, true, 2)); // getting on: the engine's flag
    CHECK(ReplayState::CopyOwnBody(true, false, false, 0));
    CHECK(ReplayState::CopyOwnBody(false, true, false, 0));
}

TEST_CASE("An action carries its owner's graph state only for copies that take actions", "[replay-state]")
{
    CHECK(ReplayState::CarriesGraphState(false, false, false));
    CHECK_FALSE(ReplayState::CarriesGraphState(true, false, false)); // a pair's half
    CHECK_FALSE(ReplayState::CarriesGraphState(false, true, false)); // its copies replay its stream
    CHECK(ReplayState::CarriesGraphState(false, true, true));        // on its own body: its copies play its actions
    CHECK_FALSE(ReplayState::CarriesGraphState(true, true, true));
}

TEST_CASE("A copy stays on its stream through its owner's pair when its partner here is a copy too", "[replay-state][pair-stream]")
{
    CHECK(ReplayState::PairOnStream(true)); // a kill move's or a mount's alike
    CHECK_FALSE(ReplayState::PairOnStream(false)); // this game's own actor, or not here: the instance or the action plays it
}

TEST_CASE("A pa_Kill action puts both actors in a kill move, as the engine's own test of its name", "[replay-state][pair-stream]")
{
    CHECK(ReplayState::EntersKillMove("pa_KillMove1HMWolfA"));
    CHECK(ReplayState::EntersKillMove("PA_KILLMOVEI")); // strnicmp
    CHECK_FALSE(ReplayState::EntersKillMove("pa_1HMShieldBash"));
    CHECK_FALSE(ReplayState::EntersKillMove("KillMove1HMWolfA")); // the victim's half
    CHECK_FALSE(ReplayState::EntersKillMove("pa_K"));
}

TEST_CASE("ReplayState: a seated copy skips its owner's get-on; one not seated plays it", "[replay][passengers]")
{
    CHECK(ReplayState::SkipsMountEntry("HorseEnter", ReplayState::kSitSeated));
    CHECK(ReplayState::SkipsMountEntry("horseEnterInstant", ReplayState::kSitSeated));
    CHECK_FALSE(ReplayState::SkipsMountEntry("HorseEnter", 0));
    CHECK_FALSE(ReplayState::SkipsMountEntry("HorseEnter", 2));
    CHECK_FALSE(ReplayState::SkipsMountEntry("HorseExit", ReplayState::kSitSeated));
}

TEST_CASE("ReplayState: a passenger's copy on this game's own mount is seated as its hold ends", "[replay][passengers]")
{
    CHECK(ReplayState::SeatsAtHoldEnd(ReplayState::kSitGettingOn, false, false));
    CHECK_FALSE(ReplayState::SeatsAtHoldEnd(ReplayState::kSitGettingOn, false, true)); // its owner's get-on still plays
    CHECK_FALSE(ReplayState::SeatsAtHoldEnd(ReplayState::kSitGettingOn, true, false)); // on its stream: its events seat it
    CHECK_FALSE(ReplayState::SeatsAtHoldEnd(0, false, false));
    CHECK_FALSE(ReplayState::SeatsAtHoldEnd(ReplayState::kSitWantsToStand, false, false));
}

TEST_CASE("ReplayState: a copy getting onto a mount collides with nothing until seated", "[replay][passengers]")
{
    CHECK(ReplayState::GettingOnMount(true, ReplayState::kSitGettingOn));
    CHECK_FALSE(ReplayState::GettingOnMount(false, ReplayState::kSitGettingOn)); // a chair's sit
    CHECK(ReplayState::GettingOnMount(true, 0)); // linked, stepping onto its mark against the horse
    CHECK_FALSE(ReplayState::GettingOnMount(true, ReplayState::kSitSeated));
    CHECK_FALSE(ReplayState::GettingOnMount(true, ReplayState::kSitWantsToStand));
    CHECK_FALSE(ReplayState::GettingOnMount(false, 0));
}

TEST_CASE("ReplayState: a copy seated on a mount takes the engine's position writes", "[replay][passengers]")
{
    CHECK(ReplayState::CopyTakesEnginePlace(true, false, 0));                         // stepped: its controller's place
    CHECK(ReplayState::CopyTakesEnginePlace(false, true, ReplayState::kSitSeated));   // its seat's sync controller
    CHECK_FALSE(ReplayState::CopyTakesEnginePlace(false, false, 0));                  // placed by the sync alone
    CHECK_FALSE(ReplayState::CopyTakesEnginePlace(false, true, ReplayState::kSitGettingOn)); // its clip, not its seat
    CHECK_FALSE(ReplayState::CopyTakesEnginePlace(false, false, ReplayState::kSitSeated));  // a chair: no mount
}

TEST_CASE("ReplayState: a mount handed over is taken once its copy ran its last owner's ops through the hand-back", "[replay][hand-back]")
{
    CHECK_FALSE(ReplayState::TakesOverMount(1000, true, 999)); // its exit pair's PairEnd still to run
    CHECK(ReplayState::TakesOverMount(1000, true, 1000));
    CHECK(ReplayState::TakesOverMount(1000, true, 1200));
    CHECK(ReplayState::TakesOverMount(0, true, 0));      // its owner left, or recorded nothing
    CHECK(ReplayState::TakesOverMount(1000, false, 0));  // off its stream: no op of its owner's will come
}

TEST_CASE("A copy's bodies take its owner's layer only while they are not its own ragdoll's", "[replay-state][copy-ragdoll]")
{
    CHECK(ReplayState::TakesOwnerBodies(false, false));
    CHECK_FALSE(ReplayState::TakesOwnerBodies(true, false));
    CHECK_FALSE(ReplayState::TakesOwnerBodies(false, true)); // a kill move's victim: its graph's ragdoll
}

TEST_CASE("A copy takes its half of a pair this game plays, and no other action of its own", "[copies][replay-state][kill-move]")
{
    using namespace ReplayState;
    CHECK_FALSE(CopyTakesAction(false, false, false));
    CHECK(CopyTakesAction(true, false, false));
    CHECK(CopyTakesAction(false, true, false));
    CHECK(CopyTakesAction(false, false, true)); // a taken NPC's half, both copies here
}

TEST_CASE("A copy's replay behind its owner still finds the answer to an earlier claim", "[copies][replay-state][kill-move]")
{
    using ReplayState::ClaimAnswer;
    ActionClaims claims;
    // Two kill moves answered before the copy here played the first: one refused, one granted as claim 4.
    claims.Set(7, {100, 0, {7, 9}, false, Payloads::Claim::kKillMove});
    claims.Set(7, {200, 4, {7, 9}, true, Payloads::Claim::kKillMove});
    CHECK(claims.Of(7, 100) == ClaimAnswer::kRefused);
    CHECK(claims.Of(7, 200) == ClaimAnswer::kClaimed);
    CHECK(claims.Of(7, 300) == ClaimAnswer::kUnknown);
    CHECK(claims.Holds(7, 9, Payloads::Claim::kKillMove));
    CHECK_FALSE(claims.Holds(7, 8, Payloads::Claim::kKillMove));
    claims.End(4); // its last member back
    CHECK(claims.Of(7, 200) == ClaimAnswer::kRefused);
    CHECK_FALSE(claims.Holds(7, 9, Payloads::Claim::kKillMove));
}

TEST_CASE("A refused kill move is skipped on every game; one on an NPC still this game's waits for its answer", "[copies][replay-state][kill-move]")
{
    using namespace ReplayState;
    // Two copies here (a spectator): refused, never entered.
    CHECK(ClaimOutcome(ClaimAnswer::kRefused, false) == PairOutcome::kSkip);
    CHECK(ClaimOutcome(ClaimAnswer::kUnknown, false) == PairOutcome::kPlay);
    CHECK(ClaimOutcome(ClaimAnswer::kClaimed, false) == PairOutcome::kPlay);
    CHECK(ClaimOutcome(ClaimAnswer::kNotNeeded, false) == PairOutcome::kPlay);
    CHECK(ClaimOutcome(ClaimAnswer::kRefused, true) == PairOutcome::kSkip);
    CHECK(ClaimOutcome(ClaimAnswer::kUnknown, true) == PairOutcome::kWait);
    CHECK(ClaimOutcome(ClaimAnswer::kClaimed, true) == PairOutcome::kPlay);
}

TEST_CASE("A grant naming a claim's member replaces that claim; held blows go by kill moves alone", "[copies][replay-state][claims]")
{
    using ReplayState::ClaimAnswer;
    ActionClaims claims;
    // A rides the horse 5; B, its passenger, drives on: B's claim replaces A's on every game.
    claims.Set(1, {0, 3, {5}, true, Payloads::Claim::kRide});
    claims.Set(2, {0, 4, {5}, true, Payloads::Claim::kRide});
    CHECK_FALSE(claims.Holds(1, 5, Payloads::Claim::kRide));
    CHECK(claims.Holds(2, 5, Payloads::Claim::kRide));
    // A ride is no kill move: a hit between rider and horse is no kill move's blow.
    CHECK_FALSE(claims.Holds(2, 5, Payloads::Claim::kKillMove));
}
